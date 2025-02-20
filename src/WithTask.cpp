#include "WithTask.h"

#include <HardwareSerial.h>

WithTask::WithTask(const char *_name): started(NULL) /*, isRunning(false)*/ {
  if (_name)
    taskName = _name;
  else
    taskName = String("WithTask#") + String(taskId++);
}

void WithTask::start() {
  task();
}

Heartbeat::Heartbeat() {
  startupTime = millis();
  sleepAt = 0;
  StartTask(Heartbeat);
}

void Heartbeat::task() {
  while (1) {
    if (sleepAt) {
      auto now = millis();
      if (now >= sleepAt) {
        cardiacArrest();
        return;
      }
      delay(sleepAt - now);
    } else {
      delay(25);
    }
  }
}

void Heartbeat::ping(uint32_t timeout) {
  auto now = millis();
  auto next = timeout + now;
  if (next > sleepAt) {
    // Serial.printf("%u ping(%u) > %u\n", now, timeout, next);
    sleepAt = next;
  } else {
    // Serial.printf("%u ping(%u) = %u\n", now, timeout, sleepAt);
  }
}

void Heartbeat::cardiacArrest(uint32_t ms) {
  // Serial.printf(F("Lifetime %f\n"), (millis() - startupTime) / 1000.0);
  // Serial.flush();
  // ddelay(200); // Just for the Serial flush

  // ...just in case the destructor needs to do real work. We don't care
  // about memory leaks/free-after-use as we're going into deep sleep
  delete this;
  esp_deep_sleep((uint64_t)ms * 1000ULL);
}
