/**
 * Copyright (c) 2023 mjcross
 *
 * SPDX-License-Identifier: BSD-3-Clause
**/

#include "esp_log.h"
#include "onewire.h"
#include "onewire_symbols.h"

extern const char *TAG;

static const rmt_symbol_word_t symbol_reset = OW_SYMBOL_RESET;

static const rmt_receive_config_t rx_config = {
    .signal_range_min_ns = OW_RX_MIN_NS,
    .signal_range_max_ns = (OW_TIMING_PARAM_H + OW_TIMING_PARAM_I) * 1000
};

static const rmt_transmit_config_t tx_config = {
    .flags.eot_level = OW_BUS_RELEASED
};


static bool _parse_reset_symbols (size_t num_symbols, rmt_symbol_word_t *symbols) {
    if (num_symbols == 0 || symbols == NULL) {
        return false;
    }
    if (num_symbols == 1 &&
        symbols[0].duration1 == 0) {
            return false;
    }
    if (num_symbols > 1 &&
        symbols[0].duration1 < OW_TIMING_PARAM_I &&
        symbols[1].duration0 + symbols[0].duration1 >= OW_TIMING_PARAM_I) {
            return true;
        }
    return false;
}


bool ow_reset (OW *ow) {
    rmt_rx_done_event_data_t evt;
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_receive (ow->rx_channel, ow->rx_buffer, ow->rx_buflen, &rx_config));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_transmit (ow->tx_channel, ow->copy_encoder, &symbol_reset, sizeof (rmt_symbol_word_t), &tx_config));
    if (xQueueReceive (ow->rx_queue, &evt, pdMS_TO_TICKS(OW_RMT_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE (TAG, "%s: rx timeout", __func__);
        return false;
    }
    bool is_present = _parse_reset_symbols (evt.num_symbols, evt.received_symbols);
    if (rmt_tx_wait_all_done (ow->tx_channel, OW_RMT_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGE (TAG, "%s: tx timeout", __func__);
    }
    return is_present;
}
