#ifndef MCU_TEMP_H
#define MCU_TEMP_H

#ifdef __cplusplus
extern "C" {
#endif

void mcu_temp_init();
float mcu_temp_read();
void mcu_temp_deinit();

#ifdef __cplusplus
}
#endif

#endif /* MCU_TEMP_H */