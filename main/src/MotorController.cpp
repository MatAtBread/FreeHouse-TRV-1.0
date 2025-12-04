#include "../trv.h"
#include "../common/gpio/gpio.hpp"
#include "MotorController.h"

#define minMotorTime 1250
#define maxMotorTime 30000


MotorController::MotorController(uint8_t pinDir, uint8_t pinSleep, BatteryMonitor* battery, uint8_t &current, int shuntMilliohms, int motorDcMilliohms) :
  pinDir(pinDir), pinSleep(pinSleep), battery(battery), current(current), shuntMilliohms(shuntMilliohms), motorDcMilliohms(motorDcMilliohms) {
  target = current;
  GPIO::pinMode(pinSleep, OUTPUT);
  GPIO::pinMode(pinDir, OUTPUT);
  setDirection(0);

  // (Todo: check if this is needed here, or when entereing sleep)
  // rtc_gpio_hold_en((gpio_num_t)NSLEEP);
  // rtc_gpio_pulldown_dis((gpio_num_t)NSLEEP);
  // rtc_gpio_pullup_en((gpio_num_t)NSLEEP);
}

MotorController::~MotorController() {
  wait();
}

int MotorController::getDirection() {
  if (GPIO::digitalRead(pinSleep) == false) return 0;
  return GPIO::digitalRead(pinDir) ? 1 : -1;
}


void MotorController::setDirection(int dir) {
  // We don't need to enable the task here, as this protected method can only be called from within the task()
  ESP_LOGI(TAG, "MotorController::setDirection %d", dir);
  switch (dir) {
    case -1:
      GPIO::digitalWrite(pinDir, false);
      GPIO::digitalWrite(pinSleep, true);
      break;
    case 1:
      GPIO::digitalWrite(pinDir, true);
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
    return 1000;//000; // Disconnected
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

  unsigned long startTime = 0;
  const char *state = "start";

  while (true) {
    auto now = millis();
    auto currentDir = getDirection();
    if (target != current) {
      auto dir = target > current ? 1 : -1;
      if (dir != currentDir) {
        setDirection(currentDir = dir);
        startTime = now;
        state = "seeking";
//        ESP_LOGI(TAG, "MotorController::task dir: %d, currentDir: %d, target %d, current %d", dir, currentDir, target, current);
      }
    }

    auto batt = battery->getValue();
    auto Rmotor = motorResistence(batt, noloadBatt, shuntMilliohms);
    auto runTime = startTime ? now - startTime : 0;
    if (startTime)
      state = "running";
      // ESP_LOGI(TAG, "MotorController::task dir: %d, noloadBatt %f, batt %f, Rmotor %f (%f, I=%fmA), target %d, current %d, runTime: %lu",
      //   currentDir, noloadBatt / 1000.0, batt / 1000.0,
      //   Rmotor / 1000.0, motorDcMilliohms / 1000.0,
      //   /* I = V / R */ (float)batt / (float)Rmotor * 1000.0,
      //   target, current, runTime);
    if (currentDir == 0) {
      noloadBatt = (noloadBatt * 7 + batt) / 8;
      state = "idle";
    } else {
      if (Rmotor >= 1000000 /* 1k ohm */) {
        state = "disconnected";
        target = 50;
        current = 50;
        startTime = 0;
        setDirection(0);
      } else if (runTime >= minMotorTime && Rmotor < motorDcMilliohms) {
        // Motor has stalled
        state = "stalled";
        // ESP_LOGI(TAG, "Motor stalled dir: %d, noloadBatt %f, batt %f, Rmotor %f (%f, I=%fmA), target %d, current %d, runTime: %lu",
        //   currentDir, noloadBatt / 1000.0, batt / 1000.0,
        //   Rmotor / 1000.0, motorDcMilliohms / 1000.0,
        //   /* I = V / R */ (float)batt / (float)Rmotor * 1000.0,
        //   target, current, runTime);
        current = target;
        setDirection(0);
        startTime = 0;
      } else if (runTime > maxMotorTime) {
        // Motor has timed-out
        state = "timed-out";
        // ESP_LOGI(TAG, "Motor timed-out");
        target = 50;
        current = 50;
        startTime = 0;
        setDirection(0);
      } else {
        current = 50 + currentDir;
      }
    }

    ESP_LOGI(TAG, "MotorController %s: dir: %d, noloadBatt %f, batt %f, Rmotor %f (%f, I=%fmA), target %d, current %d, runTime: %lu",
      state,
      currentDir, noloadBatt / 1000.0, batt / 1000.0,
      Rmotor / 1000.0, motorDcMilliohms / 1000.0,
      /* I = V / R */ (float)batt / (float)Rmotor * 1000.0,
      target, current, runTime);

    if (getDirection() || target != current)
      delay(250);
    else
      return;
  }
}
