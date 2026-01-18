#include "esp_now.h"
#include "../src/trv-state.h"
#include "../trv.h"

class EspNet : public WithTask {
protected:
  Trv *trv;
  uint8_t *joinPhrase = NULL;
  size_t joinPhraseLen = 0;
  void pair_with_hub();
  EventGroupHandle_t sendEvent;
  std::string bufferedMessage;

  void task() override;

public:
  EspNet();
  ~EspNet();
  void setTrv(Trv *trv);
  void sendStateToHub(Trv *trv); // Calls setTrv()
  void checkMessages(Trv *trv); // Calls setTrv()
  bool pendingMessages() const {
    return !bufferedMessage.empty();
  }
  static void unpair();

  // Internal referenced from statics
  void data_receive_callback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);
  void data_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status);
};
