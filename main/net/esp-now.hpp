#include "esp_now.h"

#include "../src/NetMsg.h"
#include "../trv.h"
#include "../src/trv-state.h"

class EspNet: public NetMsg {
    protected:
        Trv* trv;

    public:
        EspNet(Trv* trv);
        ~EspNet();
        void sendStateToHub(const trv_state_t &);
        void checkMessages();

        // Internal referenced from statics
        void data_receive_callback(const esp_now_recv_info_t * esp_now_info, const uint8_t *data, int data_len);
        void data_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status);
};
