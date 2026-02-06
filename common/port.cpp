
#include "port.h"
namespace GNN
{
namespace {
class DefaultRequestPort : public RequestPort {
protected:
  [[noreturn]] void blowUp() const { throw UnboundPortException(); }

public:
  DefaultRequestPort() : RequestPort("default_request_port") {}

  // Timing protocol.
  bool recvTimingResp(PacketPtr) override { blowUp(); }
  void recvReqRetry() override { blowUp(); }
};

class DefaultResponsePort : public ResponsePort {
protected:
  [[noreturn]] void blowUp() const { throw UnboundPortException(); }

public:
  DefaultResponsePort() : ResponsePort("default_response_port") {}

  // Timing protocol.
  bool recvTimingReq(PacketPtr) override { blowUp(); }
  bool tryTiming(PacketPtr) override { blowUp(); }
  void recvRespRetry() override { blowUp(); }
};
} // namespace

DefaultRequestPort defaultRequestPort;
DefaultResponsePort defaultResponsePort;

/*** FIXME:
 * The owner reference member is going through a deprecation path. In the
 * meantime, it must be initialized but no valid reference is available here.
 * Using 1 instead of nullptr prevents warning upon dereference. It should be
 * OK until definitive removal of owner.
 */
RequestPort::RequestPort(const std::string &name)
    : Port(name), _responsePort(&defaultResponsePort) {}
RequestPort::~RequestPort() {}

void RequestPort::bind(Port &peer) {
  auto *response_port = dynamic_cast<ResponsePort *>(&peer);
  _responsePort = response_port;
  Port::bind(peer);
  // response port also keeps track of request port
  _responsePort->responderBind(*this);
}

void RequestPort::unbind() {
  _responsePort->responderUnbind();
  _responsePort = &defaultResponsePort;
  Port::unbind();
}

/*** FIXME:
 * The owner reference member is going through a deprecation path. In the
 * meantime, it must be initialized but no valid reference is available here.
 * Using 1 instead of nullptr prevents warning upon dereference. It should be
 * OK until definitive removal of owner.
 */
ResponsePort::ResponsePort(const std::string &name)
    : Port(name), _requestPort(&defaultRequestPort),
      defaultBackdoorWarned(false) {}
ResponsePort::~ResponsePort() {}

void ResponsePort::responderUnbind() {
  _requestPort = &defaultRequestPort;
  Port::unbind();
}

void ResponsePort::responderBind(RequestPort &request_port) {
  _requestPort = &request_port;
  Port::bind(request_port);
}
}
