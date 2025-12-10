/**
 * Copyright (c) 2023 mjcross
 *
 * SPDX-License-Identifier: BSD-3-Clause
 **/

#include "onewire.h"
#include "onewire_symbols.h"

#define RMT_CLK_SRC_APB 4

static bool IRAM_ATTR _rx_event_done_cb(rmt_channel_handle_t rx_handle,
                                        const rmt_rx_done_event_data_t* event_data,
                                        void* ctx) {
  BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR((QueueHandle_t)ctx, event_data, &pxHigherPriorityTaskWoken);
  return (pxHigherPriorityTaskWoken == pdTRUE);
}

uint32_t ow_init(OW* ow, int gpio_num) {
  if (ow == NULL) {
    return ESP_FAIL;
  } else {
    *ow = (const OW){0};
  }

  // receiver
  rmt_rx_channel_config_t rx_channel_config = {
      .gpio_num = gpio_num,
      .clk_src = RMT_CLK_SRC_APB,
      .resolution_hz = 1000000,
      .mem_block_symbols = OW_RX_MEM_BLOCK_SYMBOLS,
      .flags = {0}};

  if (ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_new_rx_channel(&rx_channel_config, &(ow->rx_channel))) != ESP_OK) {
    return ESP_FAIL;
  }

  // rx callback
  ow->rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
  if (ow->rx_queue == NULL) {
    return ESP_FAIL;
  }

  ow->rx_buflen = OW_MAX_READ_BITS * sizeof(rmt_symbol_word_t);
  ow->rx_buffer = (rmt_symbol_word_t*)malloc(ow->rx_buflen);
  if (ow->rx_buffer == NULL) {
    return ESP_FAIL;
  }

  rmt_rx_event_callbacks_t rx_callbacks = {
      .on_recv_done = _rx_event_done_cb};

  if (ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_rx_register_event_callbacks(ow->rx_channel, &rx_callbacks, ow->rx_queue)) != ESP_OK) {
    return ESP_FAIL;
  }

  if (ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_enable(ow->rx_channel)) != ESP_OK) {
    return ESP_FAIL;
  }

  // transmitter
  rmt_tx_channel_config_t tx_channel_config = {
      .gpio_num = gpio_num,
      .clk_src = RMT_CLK_SRC_APB,
      .resolution_hz = 1000000,
      .mem_block_symbols = OW_TX_MEM_BLOCK_SYMBOLS,
      .trans_queue_depth = OW_TX_QUEUE_DEPTH,
      .flags.invert_out = true,
      .flags.io_loop_back = true,
      .flags.io_od_mode = true};

  if (ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_new_tx_channel(&tx_channel_config, &(ow->tx_channel))) != ESP_OK) {
    return ESP_FAIL;
  }

  if (ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_enable(ow->tx_channel)) != ESP_OK) {
    return ESP_FAIL;
  }

  // tx encoders
  rmt_copy_encoder_config_t copy_encoder_config = {};

  if (ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_new_copy_encoder(&copy_encoder_config, &(ow->copy_encoder))) != ESP_OK) {
    return ESP_FAIL;
  }

  rmt_bytes_encoder_config_t bytes_encoder_config = {
      .bit0 = OW_SYMBOL_0,
      .bit1 = OW_SYMBOL_1
    };

  if (ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_new_bytes_encoder(&bytes_encoder_config, &(ow->bytes_encoder))) != ESP_OK) {
    return ESP_FAIL;
  }

  return ESP_OK;
}

void ow_deinit(OW* ow) {
  if (ow->tx_channel != NULL) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_disable(ow->tx_channel));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_del_channel(ow->tx_channel));
  }
  if (ow->rx_channel != NULL) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_disable(ow->rx_channel));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_del_channel(ow->rx_channel));
  }
  if (ow->copy_encoder != NULL) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_del_encoder(ow->copy_encoder));
  }
  if (ow->bytes_encoder != NULL) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_del_encoder(ow->bytes_encoder));
  }
  if (ow->rx_buffer != NULL) {
    free(ow->rx_buffer);
  }
  if (ow->rx_queue != NULL) {
    vQueueDelete(ow->rx_queue);
  }
}
