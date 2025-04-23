#ifndef WithTask_h
#define WithTask_h

#define StartTask(Cl) (running \
  ? (_log("Task "#Cl" running"), running) \
  : (running = (_log("Task "#Cl" starting"), xTaskCreate((TaskFunction_t)&taskWrapper<Cl>, #Cl, 8192, this, 1, nullptr) == pdPASS \
    ? ((numRunning+=1),xEventGroupClearBits(anyTasks, WITHTASK_FINISHED),xEventGroupCreate()) \
    : NULL)))

#define WITHTASK_FINISHED (1 << 0)

#include <freertos/FreeRTOS.h>

template <typename C>
void taskWrapper(C* p) {
  p->start();
  vTaskDelete(NULL);
}

enum WithTaskState {
  NOT_STARTED = 1,
  TIMEOUT,
  FINISHED
};

class WithTask {
  template <typename C> friend void taskWrapper(C* p);
private:
  void start();
protected:
  EventGroupHandle_t running;
  static EventGroupHandle_t anyTasks;
  static int numRunning;

public:
  static WithTaskState waitFotAllTasks(TickType_t delay = portMAX_DELAY);

  WithTask();
  virtual ~WithTask();

  WithTaskState wait(TickType_t delay = portMAX_DELAY) {
    return running ? xEventGroupWaitBits(running, WITHTASK_FINISHED, pdFALSE, pdTRUE, delay) & WITHTASK_FINISHED ? FINISHED : TIMEOUT : NOT_STARTED;
  }

  // Pure virtual function to be implemented by derived classes
  virtual void task() = 0;
};

#endif