#include "WithTask.hpp"
#include "../trv.h"

#include "esp_log.h"

static spinlock_t spinlock = SPINLOCK_INITIALIZER;
EventGroupHandle_t WithTask::anyTasks = NULL;
int WithTask::numRunning = 0;

WithTask::WithTask() {
  running = NULL;
  spinlock_acquire(&spinlock, SPINLOCK_WAIT_FOREVER);
  if (anyTasks == NULL) {
    anyTasks = xEventGroupCreate();
    // Initially no tasks are running, so we mark it as finished.
    // If we didn't do this, waitForAllTasks would block until a task ran and
    // finished.
    if (anyTasks)
      xEventGroupSetBits(anyTasks, WITHTASK_FINISHED);
  }
  spinlock_release(&spinlock);
}

WithTaskState WithTask::wait(TickType_t delay) {
  if (running) {
    if (xEventGroupWaitBits(running, WITHTASK_FINISHED, pdFALSE, pdTRUE, delay) & WITHTASK_FINISHED) {
      running = NULL;
      return FINISHED;
    } else {
      return TIMEOUT;
    }
  } else {
    return NOT_RUNNING;
  }
}

WithTask::~WithTask() { wait(); }

WithTaskState WithTask::waitForAllTasks(TickType_t delay) {
  return anyTasks ? xEventGroupWaitBits(anyTasks, WITHTASK_FINISHED, pdFALSE,
                                        pdTRUE, delay) &
                            WITHTASK_FINISHED
                        ? FINISHED
                        : TIMEOUT
                  : NOT_RUNNING;
}

void WithTask::taskRunner(void *p) {
  WithTask *self = static_cast<WithTask *>(p);
  self->start();
  vTaskDelete(NULL);
}

EventGroupHandle_t WithTask::startTask(const char *name, int priority, int stackSize) {
  spinlock_acquire(&spinlock, SPINLOCK_WAIT_FOREVER);

  // Ensure anyTasks is initialized (redundant safety)
  if (anyTasks == NULL) {
    anyTasks = xEventGroupCreate();
    if (anyTasks)
      xEventGroupSetBits(anyTasks, WITHTASK_FINISHED);
  }

  // Clear finished bit immediately as we intend to run.
  // This transitions the global state to "Busy".
  if (numRunning == 0) {
    xEventGroupClearBits(anyTasks, WITHTASK_FINISHED);
  }
  numRunning++;

  spinlock_release(&spinlock);

  // Cleanup old running handle if it wasn't waited on
  if (running != NULL) {
    vEventGroupDelete(running);
    running = NULL;
  }

  EventGroupHandle_t newGroup = xEventGroupCreate();
  running = newGroup;

  if (xTaskCreate(taskRunner, name, stackSize, this, priority, nullptr) == pdPASS) {
    return running;
  } else {
    // Task creation failed, revert state
    if (newGroup)
      vEventGroupDelete(newGroup);
    running = NULL;

    spinlock_acquire(&spinlock, SPINLOCK_WAIT_FOREVER);
    numRunning--;
    if (numRunning == 0) {
      xEventGroupSetBits(anyTasks, WITHTASK_FINISHED);
    }
    spinlock_release(&spinlock);
    return NULL;
  }
}

void WithTask::start() {
  TaskStatus_t taskInfo = {.pcTaskName = "-"};

  vTaskGetInfo(NULL, &taskInfo, pdTRUE, eInvalid);
  ESP_LOGI(TAG, "WithTask started  %s %d %d", taskInfo.pcTaskName, numRunning);

  auto msecs = millis();
  task(); // Run user task logic
  msecs = millis() - msecs;
  ESP_LOGI(TAG, "WithTask finishing %s %u msecs", taskInfo.pcTaskName, msecs);

  if (running) {
    xEventGroupSetBits(running, WITHTASK_FINISHED);
  }

  // Log status before decrementing? Or after? original logged after, but inside
  // lock? We log after decrementing logic, but OUTSIDE lock for performance.

  spinlock_acquire(&spinlock, SPINLOCK_WAIT_FOREVER);
  numRunning--;
  int currentCount = numRunning;

  if (numRunning == 0 && anyTasks) {
    xEventGroupSetBits(anyTasks, WITHTASK_FINISHED);
  }
  spinlock_release(&spinlock);

  ESP_LOGI(TAG, "WithTask finished %s %d", taskInfo.pcTaskName, currentCount);
}
