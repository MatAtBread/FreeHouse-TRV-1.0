#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include "BatteryMonitor.h"
#include "WithTask.h"

class MotorController: public WithTask {
 protected:
  uint8_t pinDir;
  uint8_t pinSleep;
  BatteryMonitor* battery;
  volatile uint8_t target;
  volatile uint8_t& current;
  void setDirection(int dir);

 public:
  MotorController(uint8_t pinDir, uint8_t pinSleep, BatteryMonitor* battery, uint8_t& current);
  ~MotorController();
  void task();
  int getDirection();
  void setValvePosition(uint8_t pos);
  uint8_t getValvePosition();
};

#endif
