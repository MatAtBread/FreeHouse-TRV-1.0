#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include "BatteryMonitor.h"
#include "WithTask.h"

typedef struct motor_params_s {
  bool reversed;
  int backoff_ms;
  int stall_ms;
} motor_params_t;

class MotorController: public WithTask {
  friend void test_fn();
 protected:
  BatteryMonitor* battery;
  volatile uint8_t target;
  volatile uint8_t& current;
  motor_params_t& params;

  void setDirection(int dir);

 public:
  MotorController(BatteryMonitor* battery, uint8_t& current, motor_params_t &params);
  void task();
  int getDirection();
  void setValvePosition(int pos /* 0-100, -1 means "current position - stop the motor now" */);
  uint8_t getValvePosition();
  void calibrate();
  static const char* lastStatus;
};

#endif
