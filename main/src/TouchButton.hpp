#ifndef TOUCH_BUTTON_HPP
#define TOUCH_BUTTON_HPP

#include "WithTask.h"
#include "common/gpio/gpio.hpp"
#include "esp_log.h"
#include "pins.h"

// Sample the touch ADC for up to 420ms to see if it was touched for the whole
// second. Bails returning false if it was released/not touched during that
// period.
typedef enum { WAIT, PRESSED, NOT_PRESSED } TouchState;

class TouchButton : protected WithTask {
protected:
  TouchState state;

  void task() override {
    for (int i = 0;; i++) {
      auto n = GPIO::analogRead(TOUCH_PIN);
      ESP_LOGI("TRV", "Touch test %d", n);
      if (n >= 0 && n < 256) {
        state = NOT_PRESSED;
        return;
      }
      if (i == 6) {
        ESP_LOGI("TRV", "Touch button pressed");
        state = PRESSED;
        return;
      }
      vTaskDelay(70 / portTICK_PERIOD_MS);
    }
  }

public:
  TouchButton() : WithTask() { reset(); }
  bool pressed() {
    wait();
    return state == PRESSED;
  };
  void reset() {
    wait();
    state = WAIT;
    StartTask(TouchButton);
  }
};

#endif