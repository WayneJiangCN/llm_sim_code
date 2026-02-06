

 #ifndef TIMING_HH__
 #define TIMING_HH__
 
 #include "packet.h"
 namespace GNN
{

 class TimingResponseProtocol;
 
 class TimingRequestProtocol
 {
     friend class TimingResponseProtocol;
 
   protected:
     /**
      * Attempt to send a timing request to the peer by calling
      * its corresponding receive function. If the send does not
      * succeed, as indicated by the return value, then the sender must
      * wait for a recvReqRetry at which point it can re-issue a
      * sendTimingReq.
      *
      * @param peer Peer to send packet to.
      * @param pkt Packet to send.
      *
      * @return If the send was succesful or not.
      */
     bool sendReq(TimingResponseProtocol *peer, PacketPtr pkt);
 
     /**
      * Check if the peer can handle a timing request.
      *
      * If the send cannot be handled at the moment, as indicated by
      * the return value, then the sender will receive a recvReqRetry
      * at which point it can re-issue a sendTimingReq.
      *
      * @param peer Peer to send packet to.
      * @param pkt Packet to send.
      *
      * @return If the send was succesful or not.
      */
     bool trySend(TimingResponseProtocol *peer, PacketPtr pkt) const;

     /**
      * Send a retry to the peer that previously attempted a
      * sendTimingResp to this protocol and failed.
      */
     void sendRetryResp(TimingResponseProtocol *peer);
 
     /**
      * Receive a timing response from the peer.
      */
     virtual bool recvTimingResp(PacketPtr pkt) = 0;
     /**
      * Called by the peer if sendTimingReq was called on this peer (causing
      * recvTimingReq to be called on the peer) and was unsuccessful.
      */
     virtual void recvReqRetry() = 0;

 };
 
 class TimingResponseProtocol
 {
     friend class TimingRequestProtocol;
 
   protected:
     /**
      * Attempt to send a timing response to the peer by calling
      * its corresponding receive function. If the send does not
      * succeed, as indicated by the return value, then the sender must
      * wait for a recvRespRetry at which point it can re-issue a
      * sendTimingResp.
      *
      * @param peer Peer to send the packet to.
      * @param pkt Packet to send.
      *
      * @return If the send was succesful or not.
     */
     bool sendResp(TimingRequestProtocol *peer, PacketPtr pkt);
 

 
     /**
      * Send a retry to the peer that previously attempted a
      * sendTimingReq to this protocol and failed.
      */
     void sendRetryReq(TimingRequestProtocol *peer);
 
     /**
      * Receive a timing request from the peer.
      */
     virtual bool recvTimingReq(PacketPtr pkt) = 0;
 
     /**
      * Availability request from the peer.
      */
     virtual bool tryTiming(PacketPtr pkt) = 0;
 
 
     /**
      * Called by the peer if sendTimingResp was called on this
      * protocol (causing recvTimingResp to be called on the peer)
      * and was unsuccessful.
      */
     virtual void recvRespRetry() = 0;
 };
 
}
 
 #endif //__MEM_GEM5_PROTOCOL_TIMING_HH__
 