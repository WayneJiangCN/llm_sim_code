#pragma once
#include "common/port.h"
#include "common/packet.h"
#include "common/object.h"
#include "event/eventq.h"

namespace GNN
{

    class Dram : public SimObject
    {
    public:
        class DramPort : public ResponsePort
        {
            Dram *dram;

        public:
            DramPort(const std::string &_name, Dram *d);
             bool recvTimingReq(PacketPtr pkt) override;

            void recvRespRetry();
        };
        //void sendRetryReq();
        PacketPtr pendingPkt = nullptr;
        void accessAndRespond(PacketPtr pkt);
        void sendResponse();
        DramPort port;
        Dram(const std::string &_name);
        EventFunctionWrapper respEvent;
        Port &
        getPort(const std::string &if_name, int idx=-1);
        bool recvTimingReq(PacketPtr pkt);
    };

} // namespace GNN