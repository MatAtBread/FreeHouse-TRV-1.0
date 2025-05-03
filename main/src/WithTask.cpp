#include "../trv.h"
#include "WithTask.h"

#include "esp_sleep.h"

static spinlock_t spinlock = SPINLOCK_INITIALIZER;
EventGroupHandle_t WithTask::anyTasks = NULL;
int WithTask::numRunning = 0;

WithTask::WithTask() {
  running = NULL;
  spinlock_acquire(&spinlock, SPINLOCK_WAIT_FOREVER);
  if (anyTasks == NULL) {
    anyTasks = xEventGroupCreate();
  }
  spinlock_release(&spinlock);
}

WithTask::~WithTask() {
  wait();
  if (running) vEventGroupDelete(running);
  running = NULL;
}

WithTaskState WithTask::waitFotAllTasks(TickType_t delay) {
  return anyTasks ? xEventGroupWaitBits(anyTasks, WITHTASK_FINISHED, pdFALSE, pdTRUE, delay) & WITHTASK_FINISHED ? FINISHED : TIMEOUT : NOT_STARTED;
}

void WithTask::start() {
  TaskStatus_t taskInfo = {
    .pcTaskName = "-"
  };

  vTaskGetInfo(NULL, &taskInfo, pdTRUE, eInvalid);
  ESP_LOGW(TAG, "WithTask started  %s %d", taskInfo.pcTaskName, numRunning);
  task();
  xEventGroupSetBits(running, WITHTASK_FINISHED);
  running = NULL;
  ESP_LOGW(TAG, "WithTask finished %s %d", taskInfo.pcTaskName, numRunning);
  numRunning--;
  if (numRunning == 0) {
    xEventGroupSetBits(anyTasks, WITHTASK_FINISHED);
  }
}
