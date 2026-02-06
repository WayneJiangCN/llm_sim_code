/*
 * @Author: wayne 1448119477@qq.com
 * @Date: 2025-08-29 15:43:34
 * @LastEditors: wayne 1448119477@qq.com
 * @LastEditTime: 2025-08-29 15:43:35
 * @FilePath: /sim_v3/src/buffer/buffer.cpp
 * @Description: 简化的写Buffer实现
 */
#include "buffer/buffer.h"
#include "common/packet.h"
#include <algorithm>
#include <cassert>

namespace GNN {

Buffer::Buffer(const std::string &_name, int num_channels,
               size_t capacity_per_channel)
    : SimObject(_name), num_channels_(num_channels),
      maxSizePerChannel_(capacity_per_channel),
      drainEvent([this] { drainWrites(); }, _name + ".drainEvent") {
  if (num_channels_ <= 0) {
    throw std::invalid_argument("Buffer must have at least one channel");
  }

  memPorts.reserve(num_channels_);
  writeQueues.resize(num_channels_);
  outstandingWrites.assign(num_channels_, 0);
  waitingRetry.assign(num_channels_, false);

  for (int ch = 0; ch < num_channels_; ++ch) {
    memPorts.emplace_back(name() + ".buf_side" + std::to_string(ch), *this, ch);
  }
}

Buffer::~Buffer() {
  for (auto &q : writeQueues) {
    while (!q.empty()) {
      delete q.front();
      q.pop();
    }
  }
}

void Buffer::init() {
  D_DEBUG("BUFFER", "Buffer %s 初始化完成，channels=%d capacity=%zu\n",
          name().c_str(), num_channels_, maxSizePerChannel_);
}

Port &Buffer::getPort(const std::string &if_name, int idx) {
  if (if_name.find("buf_side") == 0) {
    int channel = std::stoi(if_name.substr(8));
    if (channel >= 0 && channel < num_channels_) {
      return memPorts[channel];
    }
  }
  throw std::runtime_error("No such port: " + if_name);
}

bool Buffer::isFull(int channel) const {
  if (channel < 0 || channel >= num_channels_) {
    return true;
  }
  return writeQueues[channel].size() >= maxSizePerChannel_;
}

bool Buffer::isEmpty(int channel) const {
  if (channel < 0 || channel >= num_channels_) {
    return true;
  }
  return writeQueues[channel].empty();
}

bool Buffer::enqueueWrite(int channel, addr_t addr,
                          const std::vector<storage_t> &data) {
  PacketPtr pkt = PacketManager::create_write_packet(addr, data);
  bool ok = enqueueWrite(channel, pkt);
  if (!ok) {
    delete pkt;
  }
  return ok;
}
bool Buffer::enqueueWrite(int channel, PacketPtr pkt) {
  if (channel < 0 || channel >= num_channels_) {
    D_ERROR("BUFFER", "Invalid channel %d for buffer %s", channel,
            name().c_str());
    return false;
  }

  if (!pkt->isWrite()) {
    pkt->setWrite(true);
  }

  if (writeQueues[channel].size() >= maxSizePerChannel_) {
    D_WARN("BUFFER", "Buffer %s channel %d queue full (size=%zu)",
           name().c_str(), channel, writeQueues[channel].size());
    return false;
  }

  writeQueues[channel].push(pkt);
//   D_DEBUG("BUFFER", "Buffer %s channel %d enqueue addr=0x%x size=%zu\n",
//           name().c_str(), channel, pkt->getAddr(), pkt->getSize());

  if (!drainEvent.scheduled()) {
    schedule(drainEvent, curTick() + 1);
  }

  return true;
}

void Buffer::trySendWrite(int channel) {

  if (writeQueues[channel].empty()) {
    waitingRetry[channel] = false;
    return;
  }

  PacketPtr pkt = writeQueues[channel].front();
  if (memPorts[channel].sendTimingReq(pkt)) {
    writeQueues[channel].pop();
    outstandingWrites[channel]++;
    waitingRetry[channel] = false;

    D_DEBUG("BUFFER",
            "Buffer %s channel %d 发送写请求: addr=0x%x outstanding=%zu",
            name().c_str(), channel, pkt->getAddr(),
            outstandingWrites[channel]);
  } else {
    waitingRetry[channel] = true;
    D_DEBUG("BUFFER", "Buffer %s channel %d 发送失败，等待重试",
            name().c_str(), channel);
  }
}

void Buffer::drainWrites() {
  bool still_pending = false;
  for (int ch = 0; ch < num_channels_; ++ch) {
    if (writeQueues[ch].empty()) {
      continue;
    }

    still_pending = true;

    if (waitingRetry[ch]) {
      continue;
    }

    trySendWrite(ch);
  }

  if (still_pending && !drainEvent.scheduled()) {
    schedule(drainEvent, curTick() + 1);
  }
}

bool Buffer::recvTimingResp(int channel, PacketPtr pkt) {
  if (channel < 0 || channel >= num_channels_) {
    delete pkt;
    return false;
  }

  if (outstandingWrites[channel] == 0) {
    D_WARN("BUFFER", "Buffer %s channel %d 收到意外写响应", name().c_str(),
           channel);
  } else {
    outstandingWrites[channel]--;
  }

  D_DEBUG("BUFFER",
          "Buffer %s channel %d 收到写响应 addr=0x%x outstanding=%zu\n",
          name().c_str(), channel, pkt->getAddr(), outstandingWrites[channel]);

  delete pkt;

  if (!writeQueues[channel].empty()) {
    trySendWrite(channel);
  } else if (drainEvent.scheduled()) {
    // keep drain event for other channels
  }

  return true;
}

void Buffer::handleRetry(int channel) {

  waitingRetry[channel] = false;
  trySendWrite(channel);

  bool others_pending = false;
  for (int ch = 0; ch < num_channels_; ++ch) {
    if (!writeQueues[ch].empty()) {
      others_pending = true;
      break;
    }
  }

  if (others_pending && !drainEvent.scheduled()) {
    schedule(drainEvent, curTick() + 1);
  }
}

// MemSidePort 实现
Buffer::MemSidePort::MemSidePort(const std::string &_name, Buffer &buf,
                                 int channel)
    : RequestPort(_name), buffer(buf), channel_id(channel) {}

bool Buffer::MemSidePort::recvTimingResp(PacketPtr pkt) {
  return buffer.recvTimingResp(channel_id, pkt);
}

void Buffer::MemSidePort::recvReqRetry() { buffer.handleRetry(channel_id); }

} // namespace GNN