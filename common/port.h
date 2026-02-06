
#ifndef __SIM_PORT_HH__
#define __SIM_PORT_HH__

#include <cassert>
#include <ostream>
#include <string>

#include "common.h"
#include "packet.h"
#include "timing.h"
/**
 * Ports are used to interface objects to each other.
 */
namespace GNN
{
class ResponsePort;
class Port;
class Port
{
private:
  const std::string portName;

protected:
  class UnboundPortException
  {
  };
  Port *_peer;
  bool _connected;

public:
  Port(const std::string &_name)
      : portName(_name), _peer(nullptr), _connected(false) {}
  virtual ~Port() {};
  Port &getPeer() { return *_peer; }
  const std::string name() const { return portName; }
  virtual void bind(Port &peer)
  {
    _peer = &peer;
    _connected = true;
  }
  virtual void unbind()
  {
    _peer = nullptr;
    _connected = false;
  }
  void reportUnbound() const
  {
    std::cout << portName << ":UnboundPortException" << std::endl;
  }
  bool isConnected() const { return _connected; }
  //   void takeOverFrom(Port *old) {
  //     assert(old);
  //     assert(old->isConnected());
  //     assert(!isConnected());
  //     Port &peer = old->getPeer();
  //     assert(peer.isConnected());

  //     // Disconnect the original binding.
  //     old->unbind();
  //     peer.unbind();

  //     // Connect the new binding.
  //     peer.bind(*this);
  //     bind(peer);
  //   }
  // };

  //   static inline std::ostream &operator<<(std::ostream &os, const Port
  //   &port) {
  //     os << port.name();
  //     return os;
  //   }
};
class RequestPort : public Port, public TimingRequestProtocol
{
  friend class ResponsePort;

private:


protected:
public:
  ResponsePort *_responsePort;
  RequestPort(const std::string &name);
  virtual ~RequestPort();

  /**
   * Bind this request port to a response port. This also does the
   * mirror action and binds the response port to the request port.
   */
  void bind(Port &peer) override;
  /**
   * Unbind this request port and the associated response port.
   */
  void unbind() override;

public:
  /* The functional protocol. */

  /**
   * Send a functional request packet, where the data is instantly
   * updated everywhere in the memory system, without affecting the
   * current state of any block or moving the block.
   *
   * @param pkt Packet to send.
   */
  void sendFunctional(PacketPtr pkt) const;

public:
  /* The timing protocol. */

  /**
   * Attempt to send a timing request to the responder port by calling
   * its corresponding receive function. If the send does not
   * succeed, as indicated by the return value, then the sender must
   * wait for a recvReqRetry at which point it can re-issue a
   * sendTimingReq.
   *
   * @param pkt Packet to send.
   *
   * @return If the send was succesful or not.
   */
  bool sendTimingReq(PacketPtr pkt);

  /**
   * Check if the responder can handle a timing request.
   *
   * If the send cannot be handled at the moment, as indicated by
   * the return value, then the sender will receive a recvReqRetry
   * at which point it can re-issue a sendTimingReq.
   *
   * @param pkt Packet to send.
   *
   * @return If the send was successful or not.
   */
  bool tryTiming(PacketPtr pkt) const;
  /**
   * Attempt to send a timing snoop response packet to the response
   * port by calling its corresponding receive function. If the send
   * does not succeed, as indicated by the return value, then the
   * sender must wait for a recvRetrySnoop at which point it can
   * re-issue a sendTimingSnoopResp.
   *
   * @param pkt Packet to send.
   */

  /**
   * Send a retry to the response port that previously attempted a
   * sendTimingResp to this request port and failed. Note that this
   * is virtual so that the "fake" snoop response port in the
   * coherent crossbar can override the behaviour.
   */
  virtual void sendRetryResp();

protected:
  /**
   * Called to receive an address range change from the peer response
   * port. The default implementation ignores the change and does
   * nothing. Override this function in a derived class if the owner
   * needs to be aware of the address ranges, e.g. in an
   * interconnect component like a bus.
   */

private:
};

class [[deprecated]] MasterPort : public RequestPort
{
public:
  using RequestPort::RequestPort;
};

/**
 * A ResponsePort is a specialization of a port. In addition to the
 * basic functionality of sending packets to its requestor peer, it also
 * has functions specific to a responder, e.g. to send range changes
 * and get the address ranges that the port responds to.
 *
 * The three protocols are atomic, timing, and functional, each with its own
 * header file.
 */
class ResponsePort : public Port, public TimingResponseProtocol
{
  friend class RequestPort;

private:
  RequestPort *_requestPort;

  bool defaultBackdoorWarned;

protected:
  ResponsePort(const std::string &name);
  virtual ~ResponsePort();
  /**
   * We let the request port do the work, so these don't do anything.
   */
  void unbind() override {}
  void bind(Port &peer) override {}
  bool
  tryTiming(PacketPtr pkt) override
  {
    //  std::cout << name() << std::endl;
    // panic("%s was not expecting a %s\n", name(), __func__);
    return 0;
  }

public:
  /* The timing protocol. */

  /**
   * Attempt to send a timing response to the request port by calling
   * its corresponding receive function. If the send does not
   * succeed, as indicated by the return value, then the sender must
   * wait for a recvRespRetry at which point it can re-issue a
   * sendTimingResp.
   *
   * @param pkt Packet to send.
   *
   * @return If the send was successful or not.
   */
  bool sendTimingResp(PacketPtr pkt)
  {
    bool succ = TimingResponseProtocol::sendResp(_requestPort, pkt);
    return succ;
    // try
    // {

    // }
    // catch (UnboundPortException)
    // {
    //   reportUnbound();
    //   ;
    // }
  }

  /**
   * Send a retry to the request port that previously attempted a
   * sendTimingReq to this response port and failed.
   */
  void sendRetryReq()
  {
    try
    {

      TimingResponseProtocol::sendRetryReq(_requestPort);
    }
    catch (UnboundPortException)
    {
      reportUnbound();
      ;
    }
  }

protected:
  /**
   * Called by the request port to unbind. Should never be called
   * directly.
   */
  void responderUnbind();

  /**
   * Called by the request port to bind. Should never be called
   * directly.
   */
  void responderBind(RequestPort &request_port);
};
class [[deprecated]] SlavePort : public ResponsePort
{
public:
  using ResponsePort::ResponsePort;
};
inline bool RequestPort::sendTimingReq(PacketPtr pkt)
{
  try
  {   
    bool succ = TimingRequestProtocol::sendReq(_responsePort, pkt);
    return succ;
  }
  catch (UnboundPortException)
  {
    reportUnbound();
    return false; // 明确返回
  }
}

inline bool RequestPort::tryTiming(PacketPtr pkt) const
{
  try
  {
    return TimingRequestProtocol::trySend(_responsePort, pkt);
  }
  catch (UnboundPortException)
  {
    reportUnbound();
  }
}

inline void RequestPort::sendRetryResp()
{
  try
  {
    TimingRequestProtocol::sendRetryResp(_responsePort);
  }
  catch (UnboundPortException)
  {
    reportUnbound();
  }
}
}
#endif //__SIM_PORT_HH__
