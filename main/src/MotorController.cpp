#include "../trv.h"
#include "../common/gpio/gpio.hpp"
#include "MotorController.h"

#define minMotorTime 500
#define maxMotorTimeClose 32000
#define maxMotorTimeOpen 8000
#define stallMinTime 500
#define samplePeriod 40

extern "C" {
  esp_err_t pwm_init(gpio_num_t gpio_pin);
  void pwm_set_duty(uint8_t duty);  // 0=off, 64=100% on
}

MotorController::MotorController(uint8_t pinDir, uint8_t pinSleep, BatteryMonitor* battery, uint8_t &current, motor_params_t &params) :
  pinDir(pinDir), pinSleep(pinSleep), battery(battery), current(current), params(params) {
  target = current;
  //pwm_init((gpio_num_t)pinSleep);
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
  stallCount = 0;
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

static int motorResistence(int Vmotor, int Vbatt, int Rshunt) {
  const auto deltaV = Vbatt - Vmotor;
  if (deltaV < 1)
    return 999999;// Disconnected
  return Vmotor * Rshunt / deltaV;
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


  stallCount = 0;
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

    batt = (batt * 3 + battery->getValue()) / 4;
    const auto Rmotor = motorResistence(batt, noloadBatt, params.shunt_milliohms);
    const auto runTime = startTime ? now - startTime : 0;
    if (startTime)
      state = "running";
    if (getDirection() == 0) {
      noloadBatt = (noloadBatt * 7 + batt) / 8;
      state = "idle";
    } else {
      if (Rmotor >= 1000000 /* 1k ohm */) {
        state = "disconnected";
        target = 50;
        current = 50;
        startTime = 0;
        setDirection(0);
      } else if (runTime >= minMotorTime && Rmotor < params.dc_milliohms) {
        stallCount += 1;
        state = "stalling";
        if (getDirection() > 0 || stallCount * samplePeriod >= stallMinTime) {
          // Motor has stalled
          state = "stalled";
          current = target;
          setDirection(0);
          startTime = 0;
        }
      } else if (runTime > maxMotorTime) {
        // Motor has timed-out
        startTime = 0;
        if (getDirection() == 1 && target == 100) {
          current = target;
          state = "opened";
        } else {
          state = "timed-out";
          target = current;
        }
        setDirection(0);
      } else {
        stallCount = 0;
        current = 50 + getDirection();
      }
    }

    ESP_LOGI(TAG, "\x1b[1A\rMotorController %s: dir: %d, noloadBatt %4umV, batt %4dmV (Δ%3umV, I=%3umA), Rmot %6.2f\xCE\xA9 (>%5.1f\xCE\xA9, I=%3umA), target %3d, current %3d, runTime: %5lu, timeout: %5u, stallCount: %d      ",
      state,
      getDirection(), noloadBatt, batt,
      noloadBatt - batt,
      /* I = V / R */ (1000 * (noloadBatt - batt)) / params.shunt_milliohms,
      Rmotor / 1000.0,
      params.dc_milliohms / 1000.0,
      /* I = V / R */ (1000 * batt) / Rmotor,
      target, current,
      runTime, maxMotorTime, stallCount);
      delay(samplePeriod);
  }
}
