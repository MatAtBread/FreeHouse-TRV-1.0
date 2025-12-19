#include "../trv.h"
#include "../common/gpio/gpio.hpp"
extern "C" {
  #include "pwm.h"
}
#include "MotorController.h"

#define startRunIn        50
#define runInStep         1
#define minMotorTime      250
#define maxMotorTimeClose 32000
#define maxMotorTimeOpen  10000
#define samplePeriod      40
#define battSamplePeriod  440

/* In testing:
  typical Vshunt at full stall in 0.23v (batt=4110mv, R=0.66ohms), making I=0.338mA and Rmotor=12.16-Rshunt, or 11.48ohms
  In-rush Vshunt on *reversal* is 0.38v, from stationary is 0.21v

  */

MotorController::MotorController(gpio_num_t pinDir, gpio_num_t pinSleep, BatteryMonitor* battery, uint8_t &current, motor_params_t &params) :
  pinDir(pinDir), pinSleep(pinSleep), battery(battery), current(current), params(params) {
  target = current;
  run_in = 0;
  pwm_init(pinSleep, (uint8_t)run_in);
  GPIO::pinMode(pinDir, OUTPUT);
  setDirection(0);
}

MotorController::~MotorController() {
  wait();
}

int MotorController::getDirection() {
  if (pwm_get_duty(pinSleep) == 0) return 0;
  return GPIO::digitalRead(pinDir) != params.reversed ? 1 : -1;
}


void MotorController::setDirection(int dir) {
  // We don't need to enable the task here, as this protected method can only be called from within the task()
  ESP_LOGI(TAG, "MotorController::setDirection %d", dir);
  switch (dir) {
    case -1:
      run_in = startRunIn;
      GPIO::digitalWrite(pinDir, false != params.reversed);
      break;
    case 1:
      run_in = startRunIn;
      GPIO::digitalWrite(pinDir, true != params.reversed);
      break;
    default:
      run_in = 0;
      break;
  }
  pwm_set_duty(pinSleep, (uint8_t)run_in);
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
    ESP_LOGW(TAG, "MotorController::task noloadBatt %f too low, stop", noloadBatt / 1000.0);
    target = 50;
    current = 50;
    return;
  }

  unsigned int startTime = 0;
  const char *state = "start";

  int batt = noloadBatt;
  auto maxMotorTime = getValvePosition() == 0 ? maxMotorTimeOpen : maxMotorTimeClose;
  ESP_LOGI(TAG, "MotorController %s: noloadBatt %f, target %d, current %d, timeout %d",
      state,
      noloadBatt / 1000.0,
      target, current,
      maxMotorTime);

  while (getDirection() || target != current) {
    const auto now = millis();
    if (target != current) {
      const auto dir = target > current ? 1 : -1;
      if (dir != getDirection()) {
        if (startTime)
          maxMotorTime = maxMotorTimeClose;
        setDirection(dir);
        startTime = now;
        state = "seeking";
      }
    }

    batt = (batt * (battSamplePeriod / samplePeriod) + battery->getValue()) / ((battSamplePeriod / samplePeriod) + 1);
    const auto shuntMilliVolts = noloadBatt > batt ? noloadBatt - batt : 1;
    const auto milliAmps = ((1000 * shuntMilliVolts) / params.shunt_milliohms) + 1;
    const auto stallMilliAmps = (1000 * batt) / (params.dc_milliohms + params.shunt_milliohms);
    const auto motorMilliOhms = (1000 * batt) / milliAmps;
    const auto runTime = startTime ? now - startTime : 0;
    if (startTime)
      state = "running";
    if (getDirection() == 0) {
      noloadBatt = (noloadBatt * 7 + batt) / 8;
      state = "idle";
    } else {
        state = "soft-limit";
        if (runTime >= 3000 || milliAmps < stallMilliAmps / 6) {
          state = "soft-done";
          if (run_in < 64) {
            run_in = run_in + runInStep; // 70-(25000/(runTime + 500)); -- reciprocal increments
            if (run_in > 63) run_in = 63;
            pwm_set_duty(pinSleep, (uint8_t)run_in);
            state = "soft-ramp";
          }
        }

      /*if (motorMilliOhms >= 1000000) {
        state = "disconnected";
        target = 50;
        current = 50;
        startTime = 0;
        setDirection(0);
      } else*/
      if (runTime >= minMotorTime && stallMilliAmps < milliAmps) {
        // Motor has stalled
        state = "stalled";
        current = target;
        setDirection(0);
        startTime = 0;
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

    ESP_LOGI(TAG, "\x1b[1A\r""MotorController %10s: dir: %d, run_in: %2u, noloadBatt %4dmV, batt %4dmV (Δ%3dmV, I=%3dmA), Rmot %6.2f\xCE\xA9 (>%5.1f\xCE\xA9, Istall=%3umA), target %3d, current %3d, runTime: %5lu, timeout: %5u      ",
      state,
      getDirection(),
      (unsigned int)run_in,
      noloadBatt, batt,
      shuntMilliVolts,
      milliAmps,
      motorMilliOhms / 1000.0,
      params.dc_milliohms / 1000.0,
      stallMilliAmps,
      target, current,
      runTime, maxMotorTime);
    delay(samplePeriod);
  }
}
