#ifndef MCU_TEMP_H
#define MCU_TEMP_H

// #include "WithTask.hpp"

class McuTempSensor /*: public WithTask */ {
private:
  float temp = -99.9;

//  protected:
//   void task() override;

 public:
  McuTempSensor();
  virtual ~McuTempSensor();
  float read();
};
#endif /* MCU_TEMP_H */