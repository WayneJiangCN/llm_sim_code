#include "dram_arb.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

namespace GNN {

DramArb::DramArb(const std::string &_name, int buf_size_, int num_upstreams_)
    : SimObject(_name), buf_size(buf_size_), num_upstreams(num_upstreams_),
      // 事件：仲裁和响应发送
      arbEvent([this] { arbitrate(); }, _name + ".arbEvent"),
      sendResponseEvent([this] { sendResponse(); },
                        _name + ".sendResponseEvent") {

  D_INFO("DRAM_ARB", "DramArb构造函数: num_upstreams=%d", num_upstreams);

  // 第一步：初始化基本状态
  initializeBasicState();

  // 第二步：分配多上游输入缓冲区
  allocateInputBuffers();

  // 第三步：初始化FIFO服务跟踪变量
  initializeFifoServing();

  // 第四步：创建端口
  createPorts(_name);

  // 初始化每个 bank、每个上游的响应队列
  responseQueues.resize(num_banks);
  for (int bank = 0; bank < num_banks; ++bank) {
    responseQueues[bank].resize(num_upstreams);
  }
}

void DramArb::initializeBasicState() {
  // 初始化每个bank的读请求计数
  for (int bank = 0; bank < num_banks; bank++) {
    nbrOutstandingReads[bank] = 0;
    nbrOutstandingWrites[bank] = 0;
  }
  // 初始化每个bank每个上游的重试标志
  for (int bank = 0; bank < num_banks; bank++) {
    for (int up = 0; up < num_upstreams; up++) {
      response_retryReq[bank][up] = false;  // 请求重试标志
      response_retryResp[bank][up] = false; // 响应重试标志
    }
  }
  for (int bank = 0; bank < num_banks; bank++) {
    request_retryReq[bank] = false;
  }
}

void DramArb::allocateInputBuffers() {
  // 为每个bank分配读缓冲区（每个上游一个队列）
  readInBufs.resize(num_banks);
  writeInBufs.resize(num_banks);

  for (int bank = 0; bank < num_banks; bank++) {
    readInBufs[bank].resize(num_upstreams);  // 每个bank有num_upstreams个读队列
    writeInBufs[bank].resize(num_upstreams); // 每个bank有num_upstreams个写队列
  }
}

void DramArb::initializeFifoServing() {
  // 初始化FIFO服务跟踪变量
  currentServingReadUpstream.resize(num_banks, -1);  // -1表示没有正在服务的FIFO
  currentServingWriteUpstream.resize(num_banks, -1); // -1表示没有正在服务的FIFO
}

void DramArb::createPorts(const std::string &name) {
  // 创建响应端口：每个bank有num_upstreams个，供上游连接
  responsePorts.resize(num_banks);
  for (int bank = 0; bank < num_banks; bank++) {
    for (int up = 0; up < num_upstreams; up++) {
      std::string port_name =
          name + ".response" + std::to_string(bank) + "_" + std::to_string(up);
      responsePorts[bank].emplace_back(port_name, *this, bank, up);
    }
  }

  // 创建请求端口：每个bank一个，连接下游DRAM
  for (int bank = 0; bank < num_banks; bank++) {
    std::string port_name = name + ".request" + std::to_string(bank);
    requestPorts.emplace_back(port_name, *this, bank);
  }
}

Port &DramArb::getPort(const std::string &if_name, int idx) {
  // 处理响应端口：格式为 "response<bank>_<upstream>"
  if (if_name.find("response") == 0) {
    return parseResponsePortName(if_name, idx);
  }

  // 处理请求端口：格式为 "request<bank>"
  if (if_name.find("request") == 0) {
    return parseRequestPortName(if_name);
  }

  throw std::runtime_error("未知端口名: " + if_name);
}

Port &DramArb::parseResponsePortName(const std::string &if_name, int idx) {
  // 从 "response<bank>_<upstream>" 中提取bank和upstream编号
  size_t pos = 8; // "response" 的长度
  int bank = -1;
  int up = 0;

  try {
    // 查找下划线分隔符
    size_t next = if_name.find('_', pos);

    // 提取bank编号
    std::string sbank = if_name.substr(
        pos, next == std::string::npos ? std::string::npos : next - pos);
    bank = std::stoi(sbank);

    // 提取upstream编号
    if (next != std::string::npos) {
      // 有下划线：从下划线后提取
      std::string sup = if_name.substr(next + 1);
      up = std::stoi(sup);
    } else if (idx >= 0) {
      // 无下划线但提供了idx参数
      up = idx;
    } else {
      // 无下划线且无idx：默认为0
      up = 0;
    }
  } catch (...) {
    throw std::runtime_error("响应端口名格式错误: " + if_name);
  }

  // 验证范围并返回对应端口
  if (bank >= 0 && bank < num_banks && up >= 0 && up < num_upstreams) {
    return responsePorts[bank][up];
  }

  throw std::runtime_error("端口索引超出范围: bank=" + std::to_string(bank) +
                           ", upstream=" + std::to_string(up));
}

Port &DramArb::parseRequestPortName(const std::string &if_name) {
  // 从 "request<bank>" 中提取bank编号
  int bank = std::stoi(if_name.substr(7)); // "request" 的长度

  if (bank >= 0 && bank < num_banks) {
    return requestPorts[bank];
  }

  throw std::runtime_error("请求端口索引超出范围: bank=" +
                           std::to_string(bank));
}

// 兼容旧接口：默认使用上游0
bool DramArb::recvTimingReq(PacketPtr pkt, int bank_id) {
  return recvTimingReqUp(pkt, bank_id, 0);
}

// 新接口：支持指定上游编号
bool DramArb::recvTimingReqUp(PacketPtr pkt, int bank_id, int upstream_id) {
  // 参数验证
  assert(bank_id >= 0 && bank_id < num_banks);
  assert(upstream_id >= 0 && upstream_id < num_upstreams);
  D_DEBUG("DRAM_ARB", "requst read data id :%d", upstream_id);
  // 检查是否可以接受新请求
  bool can_accept_read = nbrOutstandingReads[bank_id] < buf_size;
  bool can_accept_write = nbrOutstandingWrites[bank_id] < buf_size;

  bool accepted = false;

  if (pkt->isRead()) {
    // 处理读请求
    if (can_accept_read) {
      // 将请求放入对应上游的读缓冲区
      readInBufs[bank_id][upstream_id].push_back(pkt);
      // 记录待响应的读请求
      // id_packet cam_readInf;
      // cam_readInf.upsteam_id = upstream_id;
      // cam_readInf.packets.push(pkt);
      outstandingReads[bank_id][pkt->getAddr()].push(upstream_id);
      // outstandingUpstream[bank_id][pkt->getAddr()].push(upstream_id);
      nbrOutstandingReads[bank_id]++;
      accepted = true;
      D_INFO("DRAM_ARB", "接受读请求: bank=%d, upstream=%d,读计数=%d, addr=%d",
             bank_id, upstream_id, nbrOutstandingReads[bank_id],
             pkt->getAddr());
    }
  } else if (pkt->isWrite()) {
    if (can_accept_write) {
      // 处理写请求
      // if (nbrOutstandingWrites[bank_id] < (size_t)buf_size) {
      writeInBufs[bank_id][upstream_id].push_back(pkt);
      // 记录写请求的上游来源，用于写完成后的响应路由
      // id_packet cam_writeInf;
      // cam_writeInf.upsteam_id = upstream_id;
      // cam_writeInf.packets.push(pkt);
      nbrOutstandingWrites[bank_id]++;
      // outstandingWrites[bank_id][pkt->getAddr()].push(upstream_id);
      accepted = true;
      D_INFO("DRAM_ARB", "接受写请求: bank=%d, upstream=%d,写计数=%d, addr=%d",
             bank_id, upstream_id, writeInBufs[bank_id][upstream_id].size(),
             pkt->getAddr());
      // }
    }
  }
  if (accepted) {
    // 请求被接受，调度仲裁事件
    if (!arbEvent.scheduled()) {
      schedule(arbEvent, curTick() + 1);
    }
    return true;
  } else {
    // 请求被拒绝，设置重试标志
    response_retryReq[bank_id][upstream_id] = true;
    D_INFO("DRAM_ARB",
           "拒绝请求: bank=%d, upstream=%d, 读计数=%d, 写计数=%d, "
           "响应队列=%d,addr=%d",
           bank_id, upstream_id, nbrOutstandingReads[bank_id],
           nbrOutstandingWrites[bank_id],
           responseQueues[bank_id][upstream_id].size(), pkt->getAddr());
    return false;
  }
}

bool DramArb::recvTimingResp(PacketPtr pkt, int bank_id) {
  assert(bank_id >= 0 && bank_id < num_banks);

  addr_t addr = pkt->getAddr();
  // // 查找对应的待响应读请求
  // auto p = outstandingReads[bank_id].find(addr);
  // assert(p != outstandingReads[bank_id].end());
  // // 这是dram arb接受的addr
  // delete p->second.front();
  // p->second.pop();
  // // 如果该地址没有更多待响应请求，清理记录
  // if (p->second.empty()) {
  //   outstandingReads[bank_id].erase(p); // 需要迭代器来删除
  // }
  // 更新计数
    dram_burst_num++;
  if (pkt->isRead()) {
    // D_INFO("DRAM_ARB", "收到响应: addr=%d, bank=%d", addr, bank_id);
    // 找到对应的上游编号
    auto &qUp = outstandingReads[bank_id][addr];
    assert(!qUp.empty());
    int up = qUp.front();
    qUp.pop();
    // 如果该地址没有更多上游记录，清理
    if (qUp.empty()) {
      outstandingReads[bank_id].erase(addr); // 删除条目
    }
    // 准备发送响应
    accessAndRespond(bank_id, pkt, up);
  } else {

    // // 找到对应的上游编号
    // auto &qUp = outstandingWrites[bank_id][addr];
    // assert(!qUp.empty());
    // up = qUp.front();
    // qUp.pop();
    // // 如果该地址没有更多上游记录，清理
    // if (qUp.empty()) {
    //   outstandingWrites[bank_id].erase(addr); // 删除条目
    // }
    // D_INFO("BUG", "收到写响应，写入成功: bank=%d", bank_id);
  }
  return true;
}

void DramArb::accessAndRespond(int bank_id, PacketPtr pkt, int upstream_id) {
  // 将响应包放入该 bank 的该上游队列
  responseQueues[bank_id][upstream_id].push_back(pkt);

  D_INFO("DRAM_ARB", "准备响应: addr=%d, bank=%d, upstream=%d", pkt->getAddr(),
         bank_id, upstream_id);

  // 调度响应发送事件
  Tick delay = 1;
  Tick time = curTick() + delay;
  if (!sendResponseEvent.scheduled()) {
    schedule(sendResponseEvent, time);
  }
}

void DramArb::sendResponse() {
  // 遍历所有bank和上游，尝试发送响应
  for (int bank = 0; bank < num_banks; bank++) {
    for (int upstream_id = 0; upstream_id < num_upstreams; upstream_id++) {
      // assert(!response_retryReq[bank][upstream_id]);
      // 检查该上游是否在等待重试
      if (!response_retryResp[bank][upstream_id]) {
        // 检查是否有待发送的响应
        if (!responseQueues[bank][upstream_id].empty()) {
          auto &front = responseQueues[bank][upstream_id].front();
          PacketPtr pkt = front;

          // 尝试发送响应
          bool success = responsePorts[bank][upstream_id].sendTimingResp(pkt);

          if (success) {
            // 发送成功，从队列中移除
            responseQueues[bank][upstream_id].pop_front();

            // 如果还有更多响应，继续调度
            if (!responseQueues[bank][upstream_id].empty() &&
                !sendResponseEvent.scheduled()) {
              schedule(sendResponseEvent, curTick() + 1);
            }
          } else {
            //  std::cout<<"responsePorts[bank][upstream_id]:"<<responsePorts[bank][upstream_id].name()<<std::endl;
            // 发送失败，设置重试标志
            D_INFO("DRAM_ARB", "响应发送失败: bank=%d, upstream=%d", bank,
                   upstream_id);
            response_retryResp[bank][upstream_id] = true;
          }
        }
      }
    }
  }
}

void DramArb::handleRespRetry(int bank_id, int upstream_id) {
  assert(bank_id >= 0 && bank_id < num_banks);
  assert(upstream_id >= 0 && upstream_id < num_upstreams);

  D_INFO("DRAM_ARB", "收到响应重试: bank=%d, upstream=%d", bank_id,
         upstream_id);

  // 清除重试标志，重新尝试发送
  response_retryResp[bank_id][upstream_id] = false;
  sendResponse();
}

void DramArb::arbitrate() {

  bool has_pending = false; // 是否还有待处理的请求

  // 遍历每个bank进行仲裁
  for (int bank = 0; bank < num_banks; bank++) {
    bool sent_this_bank = false;

    // 第一步：仲裁写请求（优先级高于读）
    if (!request_retryReq[bank]) {
      sent_this_bank = arbitrateWriteRequests(bank);
      // 第二步：如果读请求没有发送，仲裁写请求
      if (!sent_this_bank)
        arbitrateReadRequests(bank);
    }
    // 检查是否还有待处理的请求
    // 如果该bank未被下游阻塞，且仍有待处理请求，则需要继续调度
    if ((!request_retryReq[bank])) {
      for (int up = 0; up < num_upstreams; up++) {
        if (!readInBufs[bank][up].empty() || !writeInBufs[bank][up].empty()) {
          has_pending = true;
          break;
        }
      }
    }
  }

  // 如果有待处理请求或需要重试，调度下一轮仲裁
  if (!arbEvent.scheduled() && (has_pending)) {
    schedule(arbEvent, curTick() + 1);
  }
}

bool DramArb::arbitrateReadRequests(int bank) {
  // 基于FIFO数据量的仲裁策略：持续服务当前FIFO直到为空
  int serving_upstream = currentServingReadUpstream[bank];

  // 如果当前有正在服务的FIFO，优先继续服务它
  if (serving_upstream >= 0 && !readInBufs[bank][serving_upstream].empty()) {
    PacketPtr pkt = readInBufs[bank][serving_upstream].front();

    if (requestPorts[bank].sendTimingReq(pkt)) {
      // 发送成功

      assert(nbrOutstandingReads[bank] > 0);
      --nbrOutstandingReads[bank];
      readInBufs[bank][serving_upstream].pop_front();
      D_DEBUG("DRAM_ARB", "发送出去的ADDR:%d", pkt->getAddr());
      D_INFO("DRAM_ARB", "继续服务读FIFO: bank=%d, upstream=%d, 剩余=%zu", bank,
             serving_upstream, readInBufs[bank][serving_upstream].size());

      // 如果该FIFO为空，清除服务状态，下次重新仲裁
      if (readInBufs[bank][serving_upstream].empty()) {
        // 当fifo有位置了，就通知所有上游buf，不然可能会漏掉
        //  如果该上游在等待重试，发送重试信号
        for (int i = 0; i < num_upstreams; i++) {
          if (response_retryReq[bank][i]) {
            responsePorts[bank][i].sendRetryReq();
            response_retryReq[bank][i] = false;
            D_INFO("DRAM_ARB", "发送重试信号: bank=%d, upstream=%d", bank, i);
          }
        }
        currentServingReadUpstream[bank] = -1;
        D_INFO("DRAM_ARB", "读FIFO已空，清除服务状态: bank=%d, upstream=%d",
               bank, serving_upstream);
      }

      return true; // 该bank本轮已发送
    } else {
      // 发送失败，保持当前服务状态，等待重试。
      // 一次失败后，该bank整体不再仲裁，直至handleReqRetry清除阻塞。
      request_retryReq[bank] = true;
      D_INFO("DRAM_ARB", "读请求发送失败: bank=%d, upstream=%d", bank,
             serving_upstream);
      return false;
    }
  }

  // 当前没有正在服务的FIFO或当前FIFO为空，需要重新仲裁
  int max_upstream = -1;
  size_t max_size = 0;

  // 找到数据量最多的upstream FIFO
  // for (int up = 0; up < num_upstreams; up++) {
  //   if (!readInBufs[bank][up].empty()) {
  // size_t current_size = readInBufs[bank][up].size();
  //     if (current_size > max_size) {
  //       max_size = current_size;
  //       max_upstream = up;
  //     }
  //   }
  // }

  for (int up = 0; up < num_upstreams; up++) {
    if (!readInBufs[bank][up].empty()) {
      max_size = readInBufs[bank][up].size();
      max_upstream = up;
      break;
    }
  }
  // 如果找到有数据的FIFO，开始服务它
  if (max_upstream >= 0) {
    PacketPtr pkt = readInBufs[bank][max_upstream].front();

    if (requestPorts[bank].sendTimingReq(pkt)) {
      // 发送成功
      // 当fifo有位置了，就通知所有上游buf，不然可能会漏掉
      // 如果该上游在等待重试，发送重试信号
      for (int i = 0; i < num_upstreams; i++) {
        if (response_retryReq[bank][i]) {
          responsePorts[bank][i].sendRetryReq();
          response_retryReq[bank][i] = false;
          D_INFO("DRAM_ARB", "发送重试信号: bank=%d, upstream=%d", bank, i);
        }
      }
      assert(nbrOutstandingReads[bank] > 0);
      --nbrOutstandingReads[bank];
      readInBufs[bank][max_upstream].pop_front();
      D_DEBUG("DRAM_ARB", "发送出去的ADDR:%d", pkt->getAddr());
      currentServingReadUpstream[bank] = max_upstream; // 设置当前服务的FIFO

      D_INFO("DRAM_ARB", "开始服务读FIFO: bank=%d, upstream=%d, fifo_size=%zu",
             bank, max_upstream, max_size);

      // 如果该FIFO为空，立即清除服务状态
      if (readInBufs[bank][max_upstream].empty()) {
        currentServingReadUpstream[bank] = -1;
        D_INFO("DRAM_ARB", "读FIFO已空，清除服务状态: bank=%d, upstream=%d",
               bank, max_upstream);
      }

      return true; // 该bank本轮已发送
    } else {
      // 发送失败后，阻塞该bank的读写仲裁
      request_retryReq[bank] = true;
      D_INFO("DRAM_ARB", "读请求发送失败: bank=%d, upstream=%d", bank,
             max_upstream);
      return false;
    }
  }

  return false;
}

bool DramArb::arbitrateWriteRequests(int bank) {
  // 基于FIFO数据量的仲裁策略：持续服务当前FIFO直到为空
  int serving_upstream = currentServingWriteUpstream[bank];

  // 如果当前有正在服务的FIFO，优先继续服务它
  if (serving_upstream >= 0 && !writeInBufs[bank][serving_upstream].empty()) {
    PacketPtr pkt = writeInBufs[bank][serving_upstream].front();

    if (requestPorts[bank].sendTimingReq(pkt)) {
      // 发送成功
      assert(nbrOutstandingWrites[bank] > 0);
      --nbrOutstandingWrites[bank];
      writeInBufs[bank][serving_upstream].pop_front();

    D_INFO("DRAM_ARB", "继续服务写FIFO: bank=%d, upstream=%d, 剩余=%zu", bank,
             serving_upstream, writeInBufs[bank][serving_upstream].size());

      // 如果该FIFO为空，清除服务状态，下次重新仲裁
      if (writeInBufs[bank][serving_upstream].empty()) {
        // 当fifo有位置了，就通知所有上游buf，不然可能会漏掉
        //  如果该上游在等待重试，发送重试信号
        for (int i = 0; i < num_upstreams; i++) {
          if (response_retryReq[bank][i]) {
            responsePorts[bank][i].sendRetryReq();
            response_retryReq[bank][i] = false;
            D_INFO("DRAM_ARB", "发送重试信号: bank=%d, upstream=%d", bank, i);
          }
        }
        currentServingWriteUpstream[bank] = -1;
        D_INFO("DRAM_ARB", "写FIFO已空，清除服务状态: bank=%d, upstream=%d",
               bank, serving_upstream);
      }

      return true; // 该bank本轮已发送
    } else {
      // 发送失败，阻塞该bank的读写仲裁
      request_retryReq[bank] = true;
      D_INFO("DRAM_ARB", "写请求发送失败: bank=%d, upstream=%d", bank,
             serving_upstream);
      return false;
    }
  }

  // 当前没有正在服务的FIFO或当前FIFO为空，需要重新仲裁
  // 优先级up_stream越小越优先
  int max_upstream = -1;
  size_t max_size = 0;

  // 找到数据量最多的upstream FIFO
  // for (int up = 0; up < num_upstreams; up++) {
  //   if (!writeInBufs[bank][up].empty()) {
  //     size_t current_size = writeInBufs[bank][up].size();
  //     if (current_size > max_size) {
  //       max_size = current_size;
  //       max_upstream = up;
  //     }
  //   }
  // }

  for (int up = 0; up < num_upstreams; up++) {
    if (!writeInBufs[bank][up].empty()) {
      max_size = writeInBufs[bank][up].size();
      max_upstream = up;
      break;
    }
  }
  // 如果找到有数据的FIFO，开始服务它
  if (max_upstream >= 0) {
    PacketPtr pkt = writeInBufs[bank][max_upstream].front();

    if (requestPorts[bank].sendTimingReq(pkt)) {
      // 发送成功
      assert(nbrOutstandingWrites[bank] > 0);
      --nbrOutstandingWrites[bank];
      writeInBufs[bank][max_upstream].pop_front();
      currentServingWriteUpstream[bank] = max_upstream; // 设置当前服务的FIFO

      D_INFO("DRAM_ARB",
             "开始服务写FIFO: bank=%d, upstream=%d, fifo_size=%zu, addr=%d",
             bank, max_upstream, max_size, pkt->getAddr());
      // 如果该FIFO为空，立即清除服务状态
      if (writeInBufs[bank][max_upstream].empty()) {
        // 当fifo有位置了，就通知所有上游buf，不然可能会漏掉
        // 如果该上游在等待重试，发送重试信号
        for (int i = 0; i < num_upstreams; i++) {
          if (response_retryReq[bank][i]) {
            responsePorts[bank][i].sendRetryReq();
            response_retryReq[bank][i] = false;
            D_INFO("DRAM_ARB", "发送重试信号: bank=%d, upstream=%d", bank, i);
          }
        }
        currentServingWriteUpstream[bank] = -1;
        D_INFO("DRAM_ARB", "写FIFO已空，清除服务状态: bank=%d, upstream=%d",
               bank, max_upstream);
      }

      return true; // 该bank本轮已发送
    } else {
      // 发送失败，阻塞该bank的读写仲裁
      request_retryReq[bank] = true;
      D_INFO("DRAM_ARB", "写请求发送失败: bank=%d, upstream=%d", bank,
             max_upstream);
      return false;
    }
  }

  return false;
}

void DramArb::scheduleArbEvent(int bank) {
  // 预留接口：可以在这里添加特定bank的仲裁调度逻辑
}

void DramArb::handleReqRetry(int bank_id) {
  assert(bank_id >= 0 && bank_id < num_banks);
  request_retryReq[bank_id] = false;
  D_INFO("DRAM_ARB", "收到请求重试: bank=%d", bank_id);

  // 调度仲裁事件，重新尝试发送
  if (!arbEvent.scheduled()) {
    schedule(arbEvent, curTick() + 1);
  }
}

} // namespace GNN