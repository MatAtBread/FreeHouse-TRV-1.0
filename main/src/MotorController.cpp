#include "../trv.h"
#include "../common/gpio/gpio.hpp"
#include "MotorController.h"

#ifdef MODEL_L1
#define maxMotorTime      10000
#define stallFactor       150
#define minMotorTime      400
#else
#define maxMotorTime      24000
#define stallFactor       180
#define minMotorTime      1000
#endif

#define samplePeriod      40
#define battSamplePeriod  200

/* In testing:
  typical Vshunt at full stall in 0.23v (batt=4110mv, R=0.66ohms), making I=0.338mA and Rmotor=12.16-Rshunt, or 11.48ohms
  In-rush Vshunt on *reversal* is 0.38v, from stationary is 0.21v
*/

static RTC_DATA_ATTR int avgAvg[3];

MotorController::MotorController(gpio_num_t pinDir, gpio_num_t pinSleep, BatteryMonitor* battery, uint8_t &current, motor_params_t &params) :
  pinDir(pinDir), pinSleep(pinSleep), battery(battery), current(current), params(params) {
  target = current;
  GPIO::pinMode(pinSleep, OUTPUT);
  GPIO::pinMode(pinDir, OUTPUT);
  setDirection(0);
}

MotorController::~MotorController() {
  wait();
}

int MotorController::getDirection() {
  if (GPIO::digitalRead(pinSleep) == false) return 0;
  return GPIO::digitalRead(pinDir) != params.reversed ? 1 : -1;
}

void MotorController::setDirection(int dir) {
  // We don't need to enable the task here, as this protected method can only be called from within the task()
  ESP_LOGI(TAG, "MotorController::setDirection %d", dir);
  switch (dir) {
    case -1:
      GPIO::digitalWrite(pinDir, false != params.reversed);
      GPIO::digitalWrite(pinSleep, true);
      break;
    case 1:
      GPIO::digitalWrite(pinDir, true != params.reversed);
      GPIO::digitalWrite(pinSleep, true);
      break;
    default:
      GPIO::digitalWrite(pinSleep, false);
      break;
  }
}

void MotorController::setValvePosition(int pos) {
  if (pos == -1) {
    target = current;
    setDirection(0);
    return;
  }
  if (pos < 0 || pos > 100) {
    ESP_LOGW(TAG, "MotorController::setValvePosition - illegal position %d", pos);
    return;
  }
  ESP_LOGI(TAG, "MotorController::setValvePosition %d %d", pos, target);
  // Let task pick up the change (we could wait until the next dreamtime)
  target = pos;
  StartTask(MotorController);
}

uint8_t MotorController::getValvePosition() {
  return current;
}

static char bar[200];
static void charChart(int b, char c) {
    if (b > sizeof(bar) - 3) b = sizeof(bar) - 2;
    bar[b] = c;
}

void MotorController::resetValve() {
  memset(avgAvg, 0, sizeof avgAvg);
}

void MotorController::calibrate() {
  ESP_LOGI(TAG,"Calibrate - stop");
  setValvePosition(-1); // Just stops the motor where it is
  wait();

  delay(250);
  resetValve();

  current = 50;
  ESP_LOGI(TAG,"Calibrate - clear state, open");
  setValvePosition(100);
  wait();
  delay(2500);
  resetValve();

  current = 50;
  ESP_LOGI(TAG,"Calibrate - close");
  setValvePosition(0);
  wait();

  delay(2500);

  ESP_LOGI(TAG,"Calibrate - open");
  setValvePosition(100);
  wait();

  ESP_LOGI(TAG,"Calibrate - done ^%d v%d", avgAvg[0], avgAvg[2]);
}

// The task depends on the members target & getDirection(), which is why we start it when any of them change
void MotorController::task() {
  // Get a stable battery level
  auto noloadBatt = battery->getValue();
  auto sample = noloadBatt;
  for (int i=0; i<10; i++) {
    delay(10);
    sample = battery->getValue();
    if (abs((signed)(noloadBatt - sample)) < 100)
      break;
    noloadBatt = (noloadBatt * 7 + sample) / 8;
  }

  if (noloadBatt < 1000) {
    ESP_LOGW(TAG, "MotorController: noloadBatt %f too low, stop", noloadBatt / 1000.0);
    target = 50;
    current = 50;
    return;
  }

  unsigned int startTime = 0;
  const char *state = "start";

  int batt = noloadBatt;
  ESP_LOGI(TAG, "MotorController %s: noloadBatt %f, target %d, current %d, timeout %d, ^%d, ^%d",
      state,
      noloadBatt / 1000.0,
      target, current,
      maxMotorTime,
      avgAvg[0], avgAvg[2]
    );

  int totalShunt = 0;
  int count = 0;

  while (getDirection() || target != current) {
    delay(samplePeriod);
    count += 1;
    const auto now = millis();
    if (target != current) {
      const auto dir = target > current ? 1 : -1;
      if (dir != getDirection()) {
        setDirection(dir);
        startTime = now;
        state = "seeking";
      }
    }

    batt = (batt * (battSamplePeriod / samplePeriod) + battery->getValue()) / ((battSamplePeriod / samplePeriod) + 1);
    const auto shuntMilliVolts = noloadBatt > batt ? noloadBatt - batt : 1;
    totalShunt += shuntMilliVolts;
    const auto avgShunt = totalShunt / count;
    const auto runTime = startTime ? now - startTime : 0;
    if (startTime)
      state = "running";
    const auto thisDir = getDirection()+1;
    if (thisDir == 1) {
      noloadBatt = (noloadBatt * 7 + batt) / 8;
      state = "idle";
    } else {
      if (avgShunt > 400) {
        state = "stuck";
        target = current;
        setDirection(0);
        return;
      } else if (runTime >= minMotorTime && (shuntMilliVolts * 100) > (stallFactor * (avgAvg[thisDir] ? avgAvg[thisDir] : avgShunt))) {
        // Motor has stalled
        state = "stalled";
        current = target;
        // Back-off
        const auto reverse = -getDirection();
        setDirection(0);
        delay(100);
        setDirection(reverse);
        delay(200);
        setDirection(0);
        startTime = 0;
        if (avgShunt > 0) {
          avgAvg[thisDir] = avgAvg[thisDir] ? ((avgAvg[thisDir] * 3) + avgShunt) / 4 : avgShunt;
        }
      } else if (runTime > maxMotorTime) {
        // Motor has timed-out
        if (getDirection() == 1 && target == 100) {
          current = target;
          state = "opened";
        } else {
          state = "timed-out";
          target = current;
        }
        startTime = 0;
        setDirection(0);
      } else {
        current = 50 + getDirection();
      }
    }


    // Longging only
    memset(bar,'-',sizeof(bar) - 1);

    charChart(avgAvg[thisDir] / 2, '#');
    charChart(avgAvg[2 - thisDir] / 2, ' ');
    charChart(shuntMilliVolts / 2, '+');
    charChart(avgShunt / 2, '|');

    bar[sizeof(bar) - 1] = 0;

    ESP_LOGI(TAG, "%s", bar);

    const auto milliAmps = ((1000 * shuntMilliVolts) / params.shunt_milliohms) + 1;
    ESP_LOGI(TAG, "MotorController %10s: dir: %d, noloadBatt %4dmV, batt %4dmV (ΔV %3dmV, ΔVavg %3dmV, I=%3dmA), Rmot %6.2f\xCE\xA9, target %3d, current %3d, runTime: %5lu, timeout: %5u    %s",
      state,
      getDirection(),
      noloadBatt, batt,
      shuntMilliVolts,
      avgShunt,
      milliAmps,
      (float)batt / (float)milliAmps,
      target, current,
      runTime, maxMotorTime,
      strcmp(state,"running") ? "" : "\x1b[1A\r") ;
  }

  ESP_LOGI(TAG,"MotorController: %s avgAvg ^%d v%d count %d, batt %d -> %d", state, avgAvg[0], avgAvg[2], count, noloadBatt, batt);
}
