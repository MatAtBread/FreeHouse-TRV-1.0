#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include "BatteryMonitor.h"
#include "WithTask.h"

typedef struct motor_params_s {
  int shunt_milliohms; // Only used for debugging
  bool reversed;
} motor_params_t;

class MotorController: public WithTask {
  friend void test_fn();
 protected:
  gpio_num_t pinDir;
  gpio_num_t pinSleep;
  BatteryMonitor* battery;
  volatile uint8_t target;
  volatile uint8_t& current;
  motor_params_t& params;
  bool calibrating;

  void setDirection(int dir);

 public:
  MotorController(gpio_num_t pinDir, gpio_num_t pinSleep, BatteryMonitor* battery, uint8_t& current, motor_params_t &params);
  ~MotorController();
  void task();
  int getDirection();
  void setValvePosition(int pos /* 0-100, -1 means "current position - stop the motor now" */);
  uint8_t getValvePosition();
  void calibrate();
  void resetValve();
};

#endif
