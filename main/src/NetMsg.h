#ifndef NETMSG_H
#define NETMSG_H
#include "trv-state.h"

#define FREEHOUSE_MODEL "TRV1"

class NetMsg {
    protected:
        void processNetMessage(const char *json, Trv* trv);
        char otaUrl[128];
        char otaSsid[32];
        char otaPwd[64];
        int messageCount;

    public:
        NetMsg() { otaUrl[0] = otaSsid[0] = otaPwd[0] = 0; messageCount = 0; }
        int getMessageCount() { return messageCount; }
        virtual ~NetMsg();

        // Implemented by derived classes for each transport type
        virtual void checkMessages() = 0;
        virtual void sendStateToHub(const trv_state_t &) = 0;
};

#endif
