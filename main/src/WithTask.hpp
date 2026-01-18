#ifndef WithTask_h
#define WithTask_h

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#define StartTask(Cl, ...) (this->startTask(#Cl, ##__VA_ARGS__))

#define WITHTASK_FINISHED (1 << 0)

enum WithTaskState {
  NOT_RUNNING = 1, // Either not yet started, or destroyed
  TIMEOUT,         // The wait timed out
  FINISHED         // The task finished
};

class WithTask {
private:
  void start();
  static void taskRunner(void *p);

protected:
  EventGroupHandle_t running;
  static EventGroupHandle_t anyTasks;
  static int numRunning;

public:
  static WithTaskState waitForAllTasks(TickType_t delay = portMAX_DELAY);

  WithTask();
  virtual ~WithTask();

  WithTaskState wait(TickType_t delay = portMAX_DELAY) {
    return running ? xEventGroupWaitBits(running, WITHTASK_FINISHED, pdFALSE,
                                         pdTRUE, delay) &
                             WITHTASK_FINISHED
                         ? ((running = NULL), FINISHED)
                         : TIMEOUT
                   : NOT_RUNNING;
  }

  EventGroupHandle_t startTask(const char *name, int priority = 2,
                               int stackSize = 8192);

  // Pure virtual function to be implemented by derived classes
  virtual void task() = 0;
};

// Looks like a task, runs like a function. Can be used to reduce context
// switching for SHORT tasks like McuTempSensor or TouchButton
class SyncTask {
private:
  WithTaskState state = NOT_RUNNING;
  void startTask(const char *name, int priority = 0, int stackSize = 0) {
    state = TIMEOUT;
    task();
    state = FINISHED;
  }

public:
  SyncTask() {}
  virtual void task() = 0;
  virtual ~SyncTask() {}
  WithTaskState wait(TickType_t timeout = portMAX_DELAY) {
    while (state == TIMEOUT) {
      vTaskDelay(2);
    };
    return state;
  }
};
#endif