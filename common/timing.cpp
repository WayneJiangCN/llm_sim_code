

#include "timing.h"
namespace GNN
{

    /* The request protocol. */

    bool
    TimingRequestProtocol::sendReq(TimingResponseProtocol *peer, PacketPtr pkt)
    {
     
        return peer->recvTimingReq(pkt);
    }

    bool
    TimingRequestProtocol::trySend(
        TimingResponseProtocol *peer, PacketPtr pkt) const
    {
        return peer->tryTiming(pkt);
    }

    void
    TimingRequestProtocol::sendRetryResp(TimingResponseProtocol *peer)
    {
        peer->recvRespRetry();
    }

    /* The response protocol. */

    bool
    TimingResponseProtocol::sendResp(TimingRequestProtocol *peer, PacketPtr pkt)
    {
        return peer->recvTimingResp(pkt);
    }

    void
    TimingResponseProtocol::sendRetryReq(TimingRequestProtocol *peer)
    {
        peer->recvReqRetry();
    }
}
