#ifndef WithTask_h
#define WithTask_h

#define StartTask(Cl) (started ? (_log("Task "#Cl" running"), started) : (started = (_log("Task "#Cl" starting"),(xTaskCreate((TaskFunction_t)&taskWrapper<Cl>, #Cl, 8192, this, 1, nullptr) == pdPASS ? this : NULL))))

#include "help.h"

#include <WString.h>
#include <freertos/FreeRTOS.h>
//#include <portmacro.h>

//#include <type_traits>

template <typename C>
void taskWrapper(C* p) {
  p->start();
  vTaskDelete(NULL);
}

class WithTask {
 private:
  inline static int taskId = 0;

public:
  WithTask *started;
  WithTask();
  virtual ~WithTask() {}

  // Pure virtual function to be implemented by derived classes
  virtual void task() = 0;
  virtual void start();
};

class Heartbeat : public WithTask {
  uint32_t sleepAt;
  uint32_t startupTime;

 public:
  Heartbeat();
  virtual ~Heartbeat(){};
  void task();
  void ping(uint32_t timeout);
  void sleep(uint32_t timeout);
  void cardiacArrest(uint32_t millis = 7500);
};
#endif