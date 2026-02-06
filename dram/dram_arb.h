/*
 * @Author: error: error: git config user.name & please set dead value or
 * install git && error: git config user.email & please set dead value or
 * install git & please set dead value or install git
 * @Date: 2025-07-29 20:49:31
 * @LastEditors: wayne 1448119477@qq.com
 * install git && error: git config user.email & please set dead value or
 * install git & please set dead value or install git
 * @LastEditTime: 2025-09-10 21:46:28
 * @FilePath: /sim_v3/src/dram/dram_arb.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置
 * 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef DRAM_ARB_H
#define DRAM_ARB_H

#include "common/packet.h"
#include "common/port.h"
#include "dram/dramsim3.h"
#include "event/eventq.h"
#include <deque>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace GNN
{
  struct id_packet {
    int upsteam_id;
    std::queue<PacketPtr> packets;
  
  };
  class DramArb : public SimObject
  {
  public:
    static constexpr int num_banks = 8;
    static constexpr int num_up = 6; // allow one extra upstream for DMA
    // 多上游数量可配置，默认1保持兼容
    DramArb(const std::string &_name, int buf_size, int num_upstreams_ = 1);
    void init() override {}
    // CAM表：addr -> 多个等待响应的请求
    std::unordered_map<addr_t, std::queue<int>> outstandingReads[num_banks];
    unsigned int nbrOutstandingReads[num_banks];

    std::unordered_map<addr_t, std::queue<int>> outstandingWrites[num_banks];
    unsigned int nbrOutstandingWrites[num_banks];
    // 响应缓存队列（每个bank一个）
    std::deque<PacketPtr> respQueue[num_banks];

    // 输入缓冲：按 bank 和上游编号分布
    // 读/写各自维护一套，以便不同优先级策略
    std::vector<std::vector<std::deque<PacketPtr>>> readInBufs;  // [bank][up]
    std::vector<std::vector<std::deque<PacketPtr>>> writeInBufs; // [bank][up]

    // 端口
    class ArbResponsePort : public ResponsePort
    {
      DramArb &arb;
      int bank_id;
      int upstream_id;

    public:
      ArbResponsePort(const std::string &name, DramArb &_arb, int _bank_id,
                      int _up_id)
          : ResponsePort(name), arb(_arb), bank_id(_bank_id),
            upstream_id(_up_id) {}
      bool recvTimingReq(PacketPtr pkt) override
      {
        return arb.recvTimingReqUp(pkt, bank_id, upstream_id);
      }
      void recvRespRetry() override { arb.handleRespRetry(bank_id, upstream_id); }
    };
    class ArbRequestPort : public RequestPort
    {
      DramArb &arb;
      int bank_id;

    public:
      ArbRequestPort(const std::string &name, DramArb &_arb, int _bank_id)
          : RequestPort(name), arb(_arb), bank_id(_bank_id) {}
      bool recvTimingResp(PacketPtr pkt) override
      {
        return arb.recvTimingResp(pkt, bank_id);
      }
      void recvReqRetry() override { arb.handleReqRetry(bank_id); }
    };

    // 多上游响应端口：每个 bank 拥有 num_upstreams 个上游连接点
    std::vector<std::vector<ArbResponsePort>> responsePorts; // [bank][up]
    std::vector<ArbRequestPort> requestPorts;

    Port &getPort(const std::string &if_name, int idx = -1);

    // 老接口保留但不再被端口调用（兼容）：统一转发到上游0
    bool recvTimingReq(PacketPtr pkt, int bank_id);
    // 新接口：携带上游编号
    bool recvTimingReqUp(PacketPtr pkt, int bank_id, int upstream_id);
    bool recvTimingResp(PacketPtr pkt, int bank_id);

    void arbitrate();
    void scheduleArbEvent(int bank);
    void handleReqRetry(int bank_id);
    void handleRespRetry(int bank_id, int upstream_id);
    void accessAndRespond(int bank_id, PacketPtr pkt, int upstream_id);
    void sendResponse();

  private:
    // 发送响应事件
    EventFunctionWrapper sendResponseEvent;
    EventFunctionWrapper arbEvent;
    //之前是所有upstream流向同一个fifo
    // std::deque<std::pair<PacketPtr, int>> responseQueue[num_banks];
       // 每个 bank、每个上游的响应队列
   std::vector<std::vector<std::deque<PacketPtr>>> responseQueues;
    int buf_size;
    bool response_retryReq[num_banks][num_up];  // 记录每个bank是否等待发送请求的重试
    bool response_retryResp[num_banks][num_up]; // 记录每个bank是否等待发送响应的重试

    bool request_retryReq[num_banks];
    int num_upstreams;
    // // 注意：不再使用轮询指针，改为基于FIFO数据量的仲裁策略
    // // 记录每个读请求的来源上游（与 outstandingReads 同步）
    // std::unordered_map<addr_t, std::queue<int>> outstandingUpstreamRead[num_banks];
    // std::unordered_map<addr_t, std::queue<int>> outstandingUpstreamWrite[num_banks];

    // 当前正在服务的FIFO跟踪：每个bank记录当前正在服务的upstream
    // -1表示没有正在服务的FIFO，需要重新仲裁
    std::vector<int> currentServingReadUpstream;  // size = num_banks
    std::vector<int> currentServingWriteUpstream; // size = num_banks

    // 私有辅助方法
    void initializeBasicState();
    void allocateInputBuffers();
    void initializeFifoServing();
    void createPorts(const std::string &name);
    Port &parseResponsePortName(const std::string &if_name, int idx);
    Port &parseRequestPortName(const std::string &if_name);
    bool arbitrateReadRequests(int bank);
    bool arbitrateWriteRequests(int bank);

  };

} // namespace GNN

#endif