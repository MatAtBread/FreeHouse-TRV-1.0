#include "esp_log.h"
#include "driver/temperature_sensor.h"

#include "hal/misc.h"
#include "soc/apb_saradc_struct.h"
#include "soc/apb_saradc_reg.h"

#include "hal/temperature_sensor_ll.h"

#include "../trv.h"

#include "mcu_temp.h"

static temperature_sensor_handle_t temp_handle = NULL;

void mcu_temp_init() {
  temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_handle));
}

float mcu_temp_read() {
  extern int16_t temp_sensor_get_raw_value(bool *range_changed);

  // Enable temperature sensor
  ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
  bool range_changed;
  int v = temp_sensor_get_raw_value(&range_changed); // in temperature_sensor.c

  // Read the raw value from the temperature sensor
  //uint32_t v = HAL_FORCE_READ_U32_REG_FIELD(APB_SARADC.saradc_apb_tsens_ctrl, saradc_tsens_out);

  // Read the calibrated value from the temperature sensor
  //float tsens_out;
  // ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &tsens_out));

  // Disable the temperature sensor if it is not needed and save the power
  ESP_ERROR_CHECK(temperature_sensor_disable(temp_handle));
  //return tsens_out;

  return (float)v; //tsens_out;
}

void mcu_temp_deinit() {
  temperature_sensor_uninstall(temp_handle);
}

/* NOT YET IMPLEMENTED
// Calibration of the internal temperature sensor
// We record the actual temp from the DS18S20 sensor and the raw value from the internal sensor
// when the device is NOT charging or recently active, so the MCU is near the ambient temperature
// This is done cyclically in an array of 8 pairs of values which are themselves averaged
// The calibration is done by linear regression on the 8 pairs of values, which gives us a slope and intercept.
// When we *are* charging or active, it then becomes possiblle to compensate the external sensor by reducing it
// slightly

// Calculate slope and intercept for arrays x and y of length n
static void linear_regression(const float *x, const float *y, int n, float *slope, float *intercept) {
  float sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
  for (int i = 0; i < n; ++i) {
      sum_x += x[i];
      sum_y += y[i];
      sum_x2 += x[i] * x[i];
      sum_xy += x[i] * y[i];
  }
  float denom = n * sum_x2 - sum_x * sum_x;
  if (denom == 0) {
      // Handle the degenerate case (all x are the same)
      *slope = 0;
      *intercept = 0;
      return;
  }
  *slope = (n * sum_xy - sum_x * sum_y) / denom;
  *intercept = (sum_y - (*slope) * sum_x) / n;
}
  */