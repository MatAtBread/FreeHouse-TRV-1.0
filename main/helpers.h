#include "esp_err.h"
#include "esp_debug_helpers.h"

#define ERR_BACKTRACE(x) ({                                         \
        esp_err_t err_rc_ = (x);                                                    \
        if (unlikely(err_rc_ != ESP_OK)) {                                          \
            _esp_error_check_failed_without_abort(err_rc_, __FILE__, __LINE__,      \
                                                  __ASSERT_FUNC, #x);               \
            esp_backtrace_print(20);                                                \
        }                                                                           \
        err_rc_;                                                                    \
    })

