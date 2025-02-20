#include "MotorController.h"

#include <esp32-hal-adc.h>
#include <esp32-hal-gpio.h>
#include <HardwareSerial.h>

#define taskStarup 1000

MotorController::MotorController(uint8_t pinDir, uint8_t pinSleep, Heartbeat* heartbeat, BatteryMonitor* battery, uint8_t &current) : pinDir(pinDir), pinSleep(pinSleep), heartbeat(heartbeat), battery(battery), current(current) {
  target = current;
  pinMode(pinSleep, OUTPUT);
  pinMode(pinDir, OUTPUT);
  pinMode(pinDir - 1, OUTPUT); // Dev board

  // Ensure the Motor driver goes off in deep sleep (Todo: check if this is needed here, or when entereing deep sleep)
  // rtc_gpio_hold_en((gpio_num_t)NSLEEP);
  // rtc_gpio_pulldown_dis((gpio_num_t)NSLEEP);
  // rtc_gpio_pullup_en((gpio_num_t)NSLEEP);
}

int MotorController::getDirection() {
  if (digitalRead(pinSleep) == LOW) return 0;
  return digitalRead(pinDir) ? -1 : 1;
}


void MotorController::setDirection(int dir) {
  heartbeat->ping(taskStarup);
  StartTask(MotorController);
  Serial.printf(F("MotorController::setDirection %d\n"), dir);
  switch (dir) {
    case 1:
      digitalWrite(pinSleep, HIGH);
      digitalWrite(pinDir, LOW);
      digitalWrite(pinDir - 1, HIGH); // Dev board
      break;
    case -1:
      digitalWrite(pinSleep, HIGH);
      digitalWrite(pinDir, HIGH);
      digitalWrite(pinDir - 1, LOW); // Dev board
      break;
    default:
      digitalWrite(pinSleep, LOW);
      digitalWrite(pinDir, LOW);
      digitalWrite(pinDir - 1, LOW); // Dev board
      break;
  }
}

void MotorController::setValvePosition(uint8_t pos) {
  if (pos != target) {
    // Let task pick up the change (we could wait until the next dreamtime)
    heartbeat->ping(taskStarup);
    StartTask(MotorController);
    target = pos;
  }
}

uint8_t MotorController::getValvePosition() {
  return current;
}

// The task depends on the members target & getDirection(), which is why we start it when any of them change
void MotorController::task() {
  // Get an average battery level
  uint32_t mv = battery->getValue();

  while (true) {
    Serial.printf("MotorController::task dir: %d, mv %d, tg %d, cp %d\n", getDirection(), mv, target, current);
    delay(299);
    if (target != current) {
      auto dir = target > current ? 1 : -1;
      if (dir != getDirection()) {
        setDirection(dir);
        continue;
      }
    }

    auto batt = battery->getValue();
    if (getDirection() == 0) {
      mv = (mv * 4 + batt) / 5;
    } else {
      if (batt < (mv*7)/8) {
        // Motor has stalled
        current = target;
        setDirection(0);
      } else {
        heartbeat->ping(600);
      }
    }
  }
}
