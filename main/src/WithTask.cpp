#include "../trv.h"
#include "WithTask.h"

#include "esp_sleep.h"

WithTask::WithTask(): started(NULL) {
}

int WithTask::numRunning = 0;

void WithTask::start() {
  TaskStatus_t taskInfo = {
    .pcTaskName = "-"
  };
  vTaskGetInfo(NULL, &taskInfo, pdTRUE, eInvalid);
  ESP_LOGW(TAG, "WithTask started  %s %d", taskInfo.pcTaskName, numRunning);
  task();
  started = NULL;
  ESP_LOGW(TAG, "WithTask finished %s %d", taskInfo.pcTaskName, numRunning);
  numRunning--;
}
