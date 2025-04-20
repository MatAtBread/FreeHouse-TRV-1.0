#ifndef WithTask_h
#define WithTask_h

#define StartTask(Cl) (started \
  ? (_log("Task "#Cl" running"), started) \
  : (started = (_log("Task "#Cl" starting"), xTaskCreate((TaskFunction_t)&taskWrapper<Cl>, #Cl, 8192, this, 1, nullptr) == pdPASS \
    ? ((WithTask::numRunning+=1),this) \
    : NULL)))

#include <freertos/FreeRTOS.h>

template <typename C>
void taskWrapper(C* p) {
  p->start();
  vTaskDelete(NULL);
}

class WithTask {
public:
  static int numRunning;
  WithTask *started;
  WithTask();
  virtual ~WithTask() {}

  // Pure virtual function to be implemented by derived classes
  virtual void task() = 0;
  void start();
};

#endif