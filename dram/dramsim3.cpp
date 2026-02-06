
#include "dram/dramsim3.h"

namespace GNN {
DRAMsim3::DRAMsim3(const std::string &name_, int channel,
                   dramsim3_wrapper *wrapper)
    : SimObject(name_), port(name() + ".port", *this), channel_id(channel),
      wrapper(wrapper), retryReq(false), retryResp(false), startTick(0),
      nbrOutstandingReads(0), nbrOutstandingWrites(0),
      sendResponseEvent([this] { sendResponse(); }, name()),
      tickEvent([this] { tick(); }, name()) {
  wrapper->set_read_callback(
      channel_id, [this](PacketPtr pkt) { this->readComplete(pkt); }),
  wrapper->set_write_callback(
      channel_id, [this](PacketPtr pkt)
    { this->writeComplete(pkt); });
  // Register a callback to compensate for the destructor not
  // being called. The callback prints the DRAMsim3 stats.
  // registerExitCallback([this]() { wrapper->printStats(); });
}

void DRAMsim3::init() {
  if (!port.isConnected()) {
    D_ERROR("DRAM", "DRAMsim3 %s is unconnected!\n", name().c_str());
  }
  // startup();
}

void DRAMsim3::startup() {

  // D_DEBUG("DRAM","tickEvent,cycle:%d ", curTick());
}

void DRAMsim3::resetStats() {
  // wrapper->resetStats();
}

void DRAMsim3::sendResponse() {
  assert(!retryResp);
  assert(!responseQueue.empty());

  bool success = port.sendTimingResp(responseQueue.front());

  if (success) {
    responseQueue.pop_front();
  } else {
    retryResp = true;
  }
}

unsigned int DRAMsim3::nbrOutstanding() const // 返回所有outstanding的请求数
{
  return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
}

void DRAMsim3::tick() {

  if (retryReq) {
    retryReq = false;
    port.sendRetryReq();
  }
  //    D_DEBUG("DRAM","tickEvent,cycle:%d ", curTick());
  // schedule(tickEvent, curTick() + 1);
}

bool DRAMsim3::recvTimingReq(PacketPtr pkt) {
  // D_DEBUG("DRAM_SIM3", "recvTimingReq:");
  // keep track of the transaction
  bool can_accept = wrapper->can_accept(pkt->getAddr(), pkt->isWrite());
  if (can_accept){
    if (!pkt->isWrite()) {
      outstandingReads[pkt->getAddr()].push(pkt);
      ++nbrOutstandingReads;

    } else {
      outstandingWrites[pkt->getAddr()].push(pkt);
      ++nbrOutstandingWrites;
    }
  }
  D_INFO("DRAM_SIM3", "accept addr : %d  can_accept: %d   pkt->isWrite() : %d",pkt->getAddr(), can_accept, pkt->isWrite());
  if (can_accept) {
    wrapper->send_request(pkt->getAddr(), pkt->isWrite());
    return true;
  } else {
    retryReq = true;
      if (retryReq&&!tickEvent.scheduled())
    schedule(tickEvent, curTick() + 1);
    return false;
  }
}

void DRAMsim3::recvRespRetry() // 被调用上游
{
  // DPRINTF(DRAMsim3, "Retrying\n");
  // 接收数据的dramarb不可能忙
  assert(retryResp);
  retryResp = false;
  sendResponse();
}

void DRAMsim3::accessAndRespond(PacketPtr pkt) {
  // DPRINTF(DRAMsim3, "Access for address %lld\n", pkt->getAddr());

  Tick delay = 1;
  Tick time = curTick() + delay;
  responseQueue.push_back(pkt);
  if (!retryResp && !sendResponseEvent.scheduled()) {
    schedule(sendResponseEvent, time);
  }
}

void DRAMsim3::readComplete(PacketPtr pkt) {
  D_INFO("DRAM_SIM3", "[Recv DRAMSIM3],channel_id: %d,readComplete addr: %lld",
         channel_id, pkt->getAddr());
  // get the outstanding reads for the address in question
  auto p = outstandingReads.find(pkt->getAddr());
  assert(p != outstandingReads.end());
  // first in first out, which is not necessarily true, but it is
  // the best we can do at this point

  p->second.pop();
  if (p->second.empty())
    outstandingReads.erase(
        p); // 会删除map中的条目，释放相关内存 这个条目对应的内存

  // no need to check for drain here as the next call will add a
  // response to the response queue straight away
  assert(nbrOutstandingReads != 0);
  --nbrOutstandingReads;
  if (retryReq&&!tickEvent.scheduled())
    schedule(tickEvent, curTick() + 1);
  // #ifdef DATA_STORE
  //   sim_storage.readPacket(pkt);
  // #else
  //   #endif
  // perform the actual memory access
  accessAndRespond(pkt);
}

void DRAMsim3::writeComplete(PacketPtr pkt) {
  D_INFO("DRAM_SIM3", "[Recv DRAMSIM3],channel_id: %d,writeComplete addr: %lld",
         channel_id, pkt->getAddr());
  auto p = outstandingWrites.find(pkt->getAddr());
  assert(p != outstandingWrites.end());

  p->second.pop();
  if (p->second.empty())
    outstandingWrites.erase(p);
  assert(nbrOutstandingWrites != 0);
  --nbrOutstandingWrites;

  if (retryReq&&!tickEvent.scheduled())

    schedule(tickEvent, curTick() + 1);

  // perform the actual memory access
  // accessAndRespond(pkt);
}

DRAMsim3::MemoryPort::MemoryPort(const std::string &_name, DRAMsim3 &_memory)
    : ResponsePort(_name), mem(_memory) {}

bool DRAMsim3::MemoryPort::recvTimingReq(PacketPtr pkt) {
  // pass it to the memory controller

  return mem.recvTimingReq(pkt);
}

void DRAMsim3::MemoryPort::recvRespRetry() { mem.recvRespRetry(); }
} // namespace GNN
