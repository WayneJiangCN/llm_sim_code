#include "DmaBuffer.h"
#include "../common/define.h"
#include "../common/file_read.h"
#include "../common/object.h"
#include "../common/packet.h"
#include "../common/port.h"
#include "../event/eventq.h"
#include <algorithm>  // for std::min
#include <cassert>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace GNN
{
  constexpr int      DmaBuffer::num_ports;
  constexpr uint32_t DmaBuffer::addr_stride;

  DmaBuffer::DmaBuffer(const std::string& name,
                       addr_t             base_addr,
                       int                burst_num,
                       int                active_banks,
                       uint32_t           total_slice_num_cfg,
                       uint32_t           total_inst_num_cfg,
                       const std::string& data_file_base_path,
                       const std::string& data_file_suffix)
    : SimObject(name), FileReader(total_slice_num_cfg, total_inst_num_cfg), base_addr_(base_addr), burst_num_(burst_num), active_banks_(active_banks),
      addr_stride_(active_banks * addr_stride), sendRespondEvent([this] { sendRespond(); }, name + ".sendRespondEvent"),
      tickEvent([this] { tick(); }, name + ".tickEvent")
  {
    setDataFilePathTemplate(data_file_base_path, data_file_suffix);
    requestPorts.reserve(num_ports);
    req_fifos_.resize(num_ports);
    for (int i = 0; i < num_ports; ++i)
    {
      current_buf_idx_[i] = true;
      requestPorts.emplace_back(name + "dma_side" + std::to_string(i), *this, i);
    }
    // 构造计算侧端口与队列
    computePorts.reserve(num_ports);
    compute_resp_fifos_.resize(num_ports);
    for (int i = 0; i < num_ports; ++i)
    {
      computePorts.emplace_back(name + "comp_side" + std::to_string(i), *this, i);
    }

    bank_controllers_.resize(active_banks_);
    for (int i = 0; i < active_banks_; ++i)
    {
      bank_controllers_[i].buffers[0].dma_pkt.reserve(burst_num_);
      bank_controllers_[i].buffers[1].dma_pkt.reserve(burst_num_);
    }
    for (int i = 0; i < active_banks_; ++i)
    {
      req_pkt_[i]           = nullptr;
      request_retryReq[i]   = false;
      response_retryResp[i] = false;
      response_retryReq[i]  = false;
      request_retryResp[i]  = false;
      recv_req_send_resp[i] = false;
      next_read_idx_[i]     = 0;
    }
    // === 新增 per-bank 状态 ===
    cmd_queues_.resize(active_banks_);
    current_cmds_.resize(active_banks_);
    inst_cnts_.resize(active_banks_, 0);
    trans_states_.resize(active_banks_, IDLE);
    lines_fetched_for_cmds_.resize(active_banks_, 0);
  }

  // DmaRequestPort inner class implementation
  DmaBuffer::DmaRequestPort::DmaRequestPort(const std::string& name, DmaBuffer& o, int id): RequestPort(name), owner(o), port_id(id)
  {
  }
  bool DmaBuffer::DmaRequestPort::recvTimingResp(PacketPtr pkt)
  {
    return owner.recvTimingResp(pkt, port_id);
  }
  void DmaBuffer::DmaRequestPort::recvReqRetry()
  {
    owner.sendRetryReq(port_id);
  }

  // ComputeSidePort implementation
  DmaBuffer::ComputeSidePort::ComputeSidePort(const std::string& name, DmaBuffer& o, int id): ResponsePort(name), owner(o), bank_id(id)
  {
  }

  bool DmaBuffer::ComputeSidePort::tryTiming(PacketPtr pkt)
  {
    // 仅当该bank存在可读FULL缓冲才允许请求
    int idx = owner.getReadableBufferIndex(bank_id);
    return idx >= 0;
  }

  bool DmaBuffer::recvTimingReq(PacketPtr pkt, uint32_t bank_id)
  {

    int idx = getReadableBufferIndex(bank_id);
    if (idx < 0)
    {
      // 没有可读数据，拒绝请求  // D_INFO("DECODER", "if_name: %s, checking
      // against: %s", if_name.c_str(),
      // computePorts[idx].name().c_str());
      D_WARN("DmaBuffer", "[DMA] recvTimingReq,没有可读数据，拒绝请求 bank_id:%d,if_name: %s", bank_id, computePorts[bank_id].name().c_str());
      response_retryReq[bank_id] = true;
      return false;
    }

    // 将请求包存储到待处理队列中，等待发送响应时使用
    // 参考Gem5模式：存储请求包，在发送响应时释放
    req_pkt_[bank_id] = pkt;

    D_INFO("DmaBuffer", "[DMA] recvTimingReq accepted for bank %d, stored pkt addr: 0x%x", bank_id, pkt->getAddr());
    recv_req_send_resp[bank_id] = true;
    if (!sendRespondEvent.scheduled())
    {

      schedule(sendRespondEvent, curTick());
    }
    return true;
  }
  void DmaBuffer::sendRespond()
  {
    // 构造响应包，搬运该FULL缓冲数据
    for (int bank = 0; bank < active_banks_; bank++)
    {
      //   assert(!response_retryResp[bank]);
      if (!response_retryResp[bank] && recv_req_send_resp[bank])
      {
        int idx = getReadableBufferIndex(bank);
        if (idx < 0)
          continue;
        assert(!bank_controllers_[bank].buffers[idx].dma_pkt.empty());
        auto& dq = bank_controllers_[bank].buffers[idx].dma_pkt;

        // D_INFO("DMA", "[DMA]dq.size()：%d,addr:%d", dq.size(),
        // dq.front()->getAddr()); D_INFO("DMA", "[DMA]dq.size()：%d,addr:%d",
        // dq.size(), dq.back()->getAddr());
        int batch = std::min(static_cast<int>(dq.size()), BURST_NUM);

        // 将前 batch 个包合并成一个包（复用第一个包作为合并后的承载）
        PacketPtr _pkt = dq.front();
        if (batch > 1)
        {
          std::vector<storage_t> merged = _pkt->getData();
          merged.reserve(merged.size() + 1024);  // 预留一些空间，避免多次扩容
          size_t total_size = _pkt->getSize();
          for (int i = 1; i < batch; ++i)
          {
            PacketPtr   p = dq[i];
            const auto& d = p->getData();
            if (!d.empty())
              merged.insert(merged.end(), d.begin(), d.end());
            total_size += p->getSize();
          }
          _pkt->setData(merged);
          _pkt->setSize(total_size);
        }

        _pkt->setBankId(bank);
        _pkt->setBufferIdx(idx);
        // 直接尝试发送响应
        bool success = computePorts[bank].sendTimingResp(_pkt);
        if (success)
        {
          recv_req_send_resp[bank] = false;
          // 注意：不要释放发送给下游的packet，应该由下游负责释放
          // PacketManager::free_packet(req_pkt_[bank]);
          // req_pkt_[bank] = nullptr;

          // 释放数据包
          if (batch <= 1)
          {
            dq.erase(dq.begin());
          }
          else
          {
            // 保留第一个（被复用发送的）不释放，由下游负责释放
            dq.erase(dq.begin());
            for (int i = 1; i < batch; ++i)
            {
              PacketManager::free_packet(dq.front());
              dq.erase(dq.begin());
            }
          }
          if (dq.empty())
            releaseBankBuffer(bank, idx);
          else
          {
            if (!sendRespondEvent.scheduled())
              schedule(sendRespondEvent, curTick() + 1);
          }
        }
        else
        {
          // 对端暂时无法接收，排入响应队列，等待对端resp retry
          D_INFO("DMA", "响应发送失败: bank=%d", bank);
          compute_resp_fifos_[bank].push_back(_pkt);
          response_retryResp[bank] = true;
        }
      }
    }
  }
  void DmaBuffer::ComputeSidePort::recvRespRetry()
  {
    // 对端可以再次接收响应，尝试发送队列中的响应
    auto& q = owner.compute_resp_fifos_[bank_id];
    if (!q.empty())
    {
      PacketPtr pkt = q.front();
      if (sendTimingResp(pkt))
      {
        q.pop_front();
        // 发送成功后释放对应缓冲
        D_INFO("DMA", "释放缓冲: bank=%d, idx=%d", bank_id, pkt->getBufferIdx());
        owner.releaseBankBuffer(bank_id, pkt->getBufferIdx());
        owner.response_retryResp[bank_id] = false;
      }
    }
  }

  void DmaBuffer::init()
  {
  }

  void DmaBuffer::enqueueCommand(const DmaCommand& cmd)
  {
    int bank = cmd.bank_id;
    if (bank < 0 || bank >= active_banks_)
    {
      D_INFO("DMA", "enqueueCommand: invalid bank_id=%d", bank);
      return;
    }
    // 检查是否还能接受新指令：每个bank两个buf进行双缓冲，最多只能有2条活跃指令
    if (inst_cnts_[bank] >= 2)
    {
      D_INFO("DMA",
             "Command %lu rejected: bank %d already has %d instructions (max 2 "
             "for dual buffer)",
             cmd.cmd_id,
             bank,
             inst_cnts_[bank]);
      return;
    }
    cmd_queues_[bank].push_back(cmd);
    D_INFO("DMA", "Command %lu enqueued to bank %d. Current inst_cnt: %d", cmd.cmd_id, bank, inst_cnts_[bank]);
    schedule_tick_if_needed();
  }

  void DmaBuffer::tick()
  {
    // 全部bank独立推进自己的DMA状态机
    for (int bank = 0; bank < active_banks_; ++bank)
    {
      // 1. 状态机驱动核心逻辑
      if (trans_states_[bank] == IDLE)
      {
        if (inst_cnts_[bank] < 2 && !cmd_queues_[bank].empty())
        {
          // 获取新命令
          current_cmds_[bank] = cmd_queues_[bank].front();
          cmd_queues_[bank].pop_front();
          lines_fetched_for_cmds_[bank] = 0;
          trans_states_[bank]           = CONFIG;
          D_INFO("DMA",
                 "[bank%d] Starting command %lu: base_addr=%#d, lines=%d, "
                 "inst_cnt will be %d",
                 bank,
                 current_cmds_[bank].cmd_id,
                 current_cmds_[bank].base_addr,
                 current_cmds_[bank].total_lines,
                 inst_cnts_[bank] + 1);
        }
      }
      if (trans_states_[bank] == CONFIG)
      {
        inst_cnts_[bank]++;
        // 每bank切换自己的buf索引
        current_buf_idx_[bank] = 1 - current_buf_idx_[bank];  // 保持原双缓冲互斥，若需bank粒度再拆
        int    ori_ch          = (current_cmds_[bank].base_addr % 0x100) / 0x40;
        addr_t dram_bias_addr  = bank >= ori_ch ? (bank - ori_ch) * addr_stride : (bank - ori_ch + active_banks_) * addr_stride;
        bank_rd_addr_[bank]    = current_cmds_[bank].base_addr + dram_bias_addr;
        auto& controller       = bank_controllers_[bank];
        int   write_idx        = current_buf_idx_[bank];
        if (controller.buffers[write_idx].state == BufferState::FILLING)
        {
          controller.dram_base_addr[write_idx]        = bank_rd_addr_[bank];
          controller.dram_final_addr[write_idx]       = bank_rd_addr_[bank] + static_cast<addr_t>(current_cmds_[bank].total_lines) * addr_stride_;
          controller.buffers[write_idx].state         = BufferState::FILLING;
          controller.buffers[write_idx].words_written = 0;
          controller.buffers[write_idx].dma_pkt.clear();
          controller.buffers[write_idx].dma_pkt.reserve(burst_num_);
          bank_transfer_active_[bank]   = true;
          controller.stalled[write_idx] = false;
          D_INFO("DMA",
                 "[bank%d] Starting command %lu: dram_base_addr=%#d, lines=%d, "
                 "inst_cnt %d, dram_final_addr=%#d",
                 bank,
                 current_cmds_[bank].cmd_id,
                 controller.dram_base_addr[write_idx],
                 current_cmds_[bank].total_lines,
                 inst_cnts_[bank],
                 controller.dram_final_addr[write_idx]);
          buf_cmd_id_[bank][write_idx]       = current_cmds_[bank].cmd_id;
          buf_cmd_callback_[bank][write_idx] = current_cmds_[bank].completion_callback;
        }
        trans_states_[bank] = STREAMING;
      }
      if (trans_states_[bank] == STREAMING)
      {
        if (lines_fetched_for_cmds_[bank] >= current_cmds_[bank].total_lines)
        {
          trans_states_[bank] = IDLE;
        }
        else
        {
          if (bank_transfer_active_[bank] && !bank_controllers_[bank].stalled[current_buf_idx_[bank]])
          {
            PacketPtr read_pkt = PacketManager::create_read_packet(bank_rd_addr_[bank], BURST_BITS / STORAGE_SIZE);
            req_fifos_[bank].push_back(read_pkt);
            addr_t final_addr_for_cmd = current_cmds_[bank].base_addr + static_cast<addr_t>(current_cmds_[bank].total_lines) * addr_stride_;
            D_INFO("DMA",
                   "[bank%d] bank_rd_addr_=%#d, final_addr_for_cmd=%#d, "
                   "lines_fetched_for_cmd_=%d",
                   bank,
                   bank_rd_addr_[bank],
                   final_addr_for_cmd,
                   lines_fetched_for_cmds_[bank]);
            bank_rd_addr_[bank] += addr_stride_;
            if (bank_rd_addr_[bank] >= final_addr_for_cmd)
            {
              bank_transfer_active_[bank] = false;
            }
          }
          lines_fetched_for_cmds_[bank]++;
        }
      }
    }
    // 2. 依然保留向所有bank发请求的全局循环
    for (int i = 0; i < num_ports; ++i)
    {
      if (!req_fifos_[i].empty())
      {
        PacketPtr pkt = req_fifos_[i].front();
        if (!request_retryReq[i] && requestPorts[i].sendTimingReq(pkt))
        {
          req_fifos_[i].pop_front();
        }
        else
          request_retryReq[i] = true;
      }
    }
    // 3. 继续调度判据（此处取任一bank尚有活跃命令即可）
    bool active_cmd = false;
    for (int bank = 0; bank < active_banks_; ++bank)
      if (trans_states_[bank] != IDLE || !cmd_queues_[bank].empty() || !request_retryReq[bank])
        active_cmd = true;
    if (active_cmd)
    {
      D_INFO("DMA", "Tick....");
      if (!tickEvent.scheduled())
      {
        schedule(tickEvent, curTick() + 1);
      }
    }
  }

  bool DmaBuffer::recvTimingResp(PacketPtr pkt, int port_id)
  {
    if (pkt->isRead())
    {
      auto&  controller = bank_controllers_[port_id];
      addr_t pkt_addr   = pkt->getAddr();

      // 根据地址区间判断数据属于哪个buf
      int target_buf_idx = -1;
      for (int buf_idx = 0; buf_idx < 2; ++buf_idx)
      {
        if (pkt_addr >= controller.dram_base_addr[buf_idx] && pkt_addr < controller.dram_final_addr[buf_idx])
        {
          target_buf_idx = buf_idx;
          break;
        }
      }

      if (target_buf_idx == -1)
      {
        D_INFO("DMA", "无法确定地址 %#x 属于哪个buf，拒绝响应", pkt_addr);
        return false;
      }

      auto& buffer = controller.buffers[target_buf_idx];
      D_INFO("DMA", "Bank %d 地址 %#d 路由到 buf %d, state: %d", port_id, pkt_addr, target_buf_idx, static_cast<int>(buffer.state));

      if (buffer.state == BufferState::FILLING)
      {
        int idx = (pkt_addr - controller.dram_base_addr[target_buf_idx]) / addr_stride_;
        //     if(port_id ==2)
        //  D_INFO("CAM", "idx :%d, bank_id:%d,addr %d idx %d  size
        //  %d",target_buf_idx,port_id,pkt_addr, idx,buffer.dma_pkt.size());
        assert(idx <= burst_num_);
        assert(pkt != nullptr);
        if (idx >= static_cast<int>(buffer.dma_pkt.size()))
          buffer.dma_pkt.resize(idx + 1);

        buffer.dma_pkt[idx]   = pkt;
        //  (!buffer.dma_pkt.empty() && buffer.dma_pkt.front() != nullptr)
        // D_INFO("DMA", "[DMA]dq.size()：%d,addr:%#d", buffer.dma_pkt.size(),
        // buffer.dma_pkt.front()->getAddr());
        //  D_INFO("DMA", "[DMA]dq.size()：%d,addr back:%#d",
        //  buffer.dma_pkt.size(), buffer.dma_pkt.back()->getAddr());
        buffer.words_written += 1;
        D_INFO("DMA", " buffer.words_written   %d", buffer.words_written);
        if (buffer.words_written >= burst_num_)
        {
          buffer.state = BufferState::FULL;
          // 使用该buf对应的命令ID，而不是当前命令ID
          D_INFO("DMA", "Bank %d Buffer %d is FULL for cmd %lu.", port_id, target_buf_idx, buf_cmd_id_[port_id][target_buf_idx]);

          // 检查该buf对应的命令是否完成
          check_buf_cmd_completion(target_buf_idx, port_id);
          if (target_buf_idx == next_read_idx_[port_id])
            D_DEBUG("DMA",
                    "Bank %d Buffer %d is FULL for cmd %lu. next_read_idx_ "
                    "%d,response_retryReq %d",
                    port_id,
                    target_buf_idx,
                    buf_cmd_id_[port_id][target_buf_idx],
                    next_read_idx_[port_id],
                    response_retryReq[port_id]);
          maybe_notify_compute_full(port_id);

          // 不再自动切换到下一个buf，因为buf选择由命令控制
          controller.stalled[target_buf_idx] = true;  // 当前buf已满，暂停该bank的该buf
        }
      }
      else if (buffer.state == BufferState::FULL)
      {
        D_INFO("DMA",
               "目标buf %d 已满，无法再接收DRAM响应，标记重试并拒绝 DMA请求: "
               "bank=%d",
               target_buf_idx,
               port_id);
        // 目标buf已满，无法再接收DRAM响应，标记重试并拒绝
        request_retryResp[port_id] = true;
        return false;
      }
      else
      {
        D_INFO("DMA", "目标buf %d 状态为 %d，无法接收数据", target_buf_idx, static_cast<int>(buffer.state));
        return false;
      }
    }

    return true;
  }

  void DmaBuffer::check_buf_cmd_completion(int buf_idx, int port_id)
  {
    // 检查该buf对应的命令是否完成：只要有一个bank的该buf已填满即可

    if (bank_controllers_[port_id].buffers[buf_idx].state == BufferState::FULL)
    {
      // 该buf对应的命令完成
      uint64_t completed_cmd_id = buf_cmd_id_[port_id][buf_idx];
      auto     callback         = buf_cmd_callback_[port_id][buf_idx];

      D_INFO("DMA",
             "Command %lu completed (DMA fetch part). At least one of %d banks "
             "buf %d for bank %d is FULL. Firing callback.",
             completed_cmd_id,
             active_banks_,
             buf_idx,
             port_id);

      if (callback)
      {
        callback(completed_cmd_id);
        // 清空回调，防止重复调用
        buf_cmd_callback_[port_id][buf_idx] = nullptr;
      }

      // 如果这是当前命令，则返回IDLE状态
      if (buf_idx == current_buf_idx_[port_id])
      {
        trans_states_[port_id] = IDLE;
      }
    }
  }

  void DmaBuffer::releaseBankBuffer(int bank_id, int buffer_idx)
  {
    assert(bank_id >= 0 && bank_id < active_banks_ && (buffer_idx == 0 || buffer_idx == 1));
    auto& controller = bank_controllers_[bank_id];

    if (controller.buffers[buffer_idx].state == BufferState::FULL)
    {
      controller.buffers[buffer_idx].state         = BufferState::FILLING;
      controller.buffers[buffer_idx].words_written = 0;
      controller.buffers[buffer_idx].dma_pkt.clear();
      controller.buffers[buffer_idx].dma_pkt.reserve(burst_num_);

      // D_INFO("DMA", "Bank %d Buffer %d released by consumer.", bank_id,
      // buffer_idx);
      next_read_idx_[bank_id] = !next_read_idx_[bank_id];

      // 检查是否所有bank的指定buf都已被释放
      // bool all_specified_bufs_released = true;
      // for (int i = 0; i < active_banks_; ++i)
      // {
      //   auto &ctrl = bank_controllers_[i];
      //   if (ctrl.buffers[buffer_idx].state == BufferState::FULL)
      //   {
      //     all_specified_bufs_released = false;
      //     break;
      //   }
      // }
      inst_cnts_[bank_id]--;
      assert(inst_cnts_[bank_id] >= 0);
      CompleteCommand(bank_id);
      // 如果该bank的该buf被暂停，现在可以恢复
      if (controller.stalled[buffer_idx])
      {
        controller.stalled[buffer_idx] = false;
        // D_INFO("DMA", "Bank %d buf %d unstalled after release.", bank_id,
        // buffer_idx);
      }

      // 如果之前由于写缓冲满而拒绝过DRAM响应，则在释放后通知上游重试
      if (request_retryResp[bank_id])
      {
        // D_INFO("DMA", "Bank %d request retryResp.", bank_id);
        request_retryResp[bank_id] = false;
        requestPorts[bank_id].sendRetryResp();
      }
    }
    schedule_tick_if_needed();
  }

  int DmaBuffer::getReadableBufferIndex(int bank_id) const
  {
    const auto& controller = bank_controllers_[bank_id];
    bool        full0      = controller.buffers[current_buf_idx_[bank_id]].state == BufferState::FULL;
    bool        full1      = controller.buffers[1 - current_buf_idx_[bank_id]].state == BufferState::FULL;
    // D_INFO("DMA", "full0 : %d full1 : %d next_id :
    // %d",static_cast<int>(full0),static_cast<int>(full1),next_read_idx_[bank_id]);
    if (full0 || full1)
      // 轮转选择，避免总是取同一侧
      if (next_read_idx_[bank_id] == current_buf_idx_[bank_id] && full0)
        return next_read_idx_[bank_id];
      else if (next_read_idx_[bank_id] == 1 - current_buf_idx_[bank_id] && full1)
        return next_read_idx_[bank_id];
    return -1;
  }
  void DmaBuffer::sendRetryReq(int port_id)
  {
    request_retryReq[port_id] = false;
    schedule_tick_if_needed();
  }

  void DmaBuffer::schedule_tick_if_needed()
  {
    for (int bank = 0; bank < active_banks_; ++bank)
    {
      if (!tickEvent.scheduled())
      {
        schedule(tickEvent, curTick() + 1);
      }
    }
  }

  Port& DmaBuffer::getPort(const std::string& if_name, int idx)
  {
    // D_INFO("DECODER", "if_name: %s, checking against: %s", if_name.c_str(),
    // computePorts[idx].name().c_str());
    if (if_name.rfind(requestPorts[idx].name(), 0) == 0)
    {
      int bank = std::stoi(if_name.substr(if_name.length() - 1));
      if (bank >= 0 && bank < num_ports)
        return requestPorts[bank];
    }
    if (if_name.rfind(computePorts[idx].name(), 0) == 0)
    {
      int bank = std::stoi(if_name.substr(if_name.length() - 1));
      if (bank >= 0 && bank < num_ports)
        return computePorts[bank];
    }
    throw std::runtime_error("No such port: " + if_name);
  }

  void DmaBuffer::maybe_notify_compute_full(int bank_id)
  {
    // 如果该bank之前有请求被拒，或当前存在FULL缓冲，尝试唤醒计算侧
    if (response_retryReq[bank_id])
    {
      response_retryReq[bank_id] = false;
      D_DEBUG("CAM", "Bank %d response_retryReq.", bank_id);
      computePorts[bank_id].sendRetryReq();
    }
    else
    {
      // 可选的主动通知：根据需要开启
      // computePorts[bank_id].sendRetryReq();
    }
  }

}  // namespace GNN
