#include "../trv.h"
#include "WithTask.h"

// #include <HardwareSerial.h>

WithTask::WithTask(): started(NULL) /*, isRunning(false)*/ {
}

void WithTask::start() {
  task();
  started = NULL;
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
  TaskStatus_t taskInfo;
  vTaskGetInfo(NULL, &taskInfo, pdTRUE, eInvalid);
  if (next > sleepAt) {
    _log("%s %lu ping(%lu) > %lu", taskInfo.pcTaskName, now, timeout, next);
    sleepAt = next;
  } else {
//    _log("%s %u ping(%u) = %u\n", taskInfo.pcTaskName, now, timeout, sleepAt);
  }
}

void Heartbeat::sleep(uint32_t timeout) {
  ping(timeout + 50);
  delay(timeout);
}

void Heartbeat::cardiacArrest(uint32_t ms) {
  // _log("Lifetime %f\n"), (millis() - startupTime) / 1000.0);
  // Serial.flush();
  // ddelay(200); // Just for the Serial flush

  // ...just in case the destructor needs to do real work. We don't care
  // about memory leaks/free-after-use as we're going into deep sleep
  delete this;
  esp_deep_sleep((uint64_t)ms * 1000ULL);
}
