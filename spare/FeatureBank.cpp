#include "FeatureBank.h"
#include "../common/debug.h"
#include <cassert>

namespace GNN
{

  FeatureBank::FeatureBank(const std::string &name, addr_t base_addr,
                           int burst_num, int active_banks)
      : DmaBuffer(name, base_addr, burst_num, active_banks, 2, 2,
                  "./data/layer0_feature_", ".txt"),
        selfScheduleEvent([this]
                          { startNextDmaCommand(); },
                          name + ".SelfScheduleEvent")
  {
    cmd_id_cnts_.resize(active_banks_, 0);
    buffer_is_clear_flag.resize(active_banks_, false);
  }

  void FeatureBank::init()
  {
    startNextDmaCommand();
    startNextDmaCommand();
  }

  bool FeatureBank::recvTimingReq(PacketPtr pkt, uint32_t bank_id)
  {
    if (pkt->getFeatureBufferIsClear())
    {
      buffer_is_clear_flag[bank_id] = true;
      D_INFO("FeatureBank", "Bank %d: Buffer clear", bank_id);
    }

    for (int i = 0; i < active_banks_; ++i)
    {
      int idx = getReadableBufferIndex(i);
      if (idx < 0)
      {
        D_WARN("FeatureBank", " bank_id:%d i:%d All buffers not full yet", bank_id, i);
        response_retryReq[bank_id] = true;
        return false;
      }
      else
      {
        maybe_notify_compute_full(i);
      }
    }

    req_pkt_[bank_id] = pkt;
    recv_req_send_resp[bank_id] = true;
    if (!sendRespondEvent.scheduled())
    {
      schedule(sendRespondEvent, curTick());
    }
    return true;
  }

  void FeatureBank::startNextDmaCommand()
  {
    for (int i = 0; i < active_banks_; ++i)
    {
      if (inst_cnts_[i] >= 2)
      {
        D_WARN("FeatureBank", "Bank%d instruction counter full (inst_cnt=2)", i);
        continue;
      }
      addr_t addr = base_addr_ + cmd_id_cnts_[i] * INST_ADDR_STRIDE * burst_num_;
      startFeatureLoadCommand(cmd_id_cnts_[i]++, addr, burst_num_, i);
    }
  }

  void FeatureBank::startFeatureLoadCommand(uint64_t cmd_id, addr_t base_addr,
                                            int total_lines, int bank_id)
  {
    DmaCommand cmd;
    assert(base_addr < 0xffffffff);
    cmd.bank_id = bank_id;
    cmd.cmd_id = cmd_id;
    cmd.base_addr = base_addr;
    cmd.total_lines = total_lines;
     D_INFO("INST", "Bank %d: Start cmd %lu, addr=0x%x, lines=%d if_name.c_str():%s",
           bank_id, cmd_id, base_addr, total_lines,name().c_str());
    enqueueCommand(cmd);
  }

  void FeatureBank::CompleteCommand(uint64_t bank_id)
  {
    // if (cmd_id_cnts_[bank_id] < TOTAL_INST_NUM_CFG * 100) {
    addr_t addr = base_addr_ + cmd_id_cnts_[bank_id] * INST_ADDR_STRIDE * burst_num_;
    startFeatureLoadCommand(cmd_id_cnts_[bank_id]++, addr, burst_num_, bank_id);
    // }
  }

  void FeatureBank::sendRespond()
  {
    for (int bank = 0; bank < active_banks_; bank++)
    {
      if (response_retryResp[bank] || !recv_req_send_resp[bank])
        continue;

      int idx = getReadableBufferIndex(bank);
      if (idx < 0)
        continue;

      assert(!bank_controllers_[bank].buffers[idx].dma_pkt.empty());

      PacketPtr pkt = req_pkt_[bank];
      if (computePorts[bank].sendTimingResp(pkt))
      {
        recv_req_send_resp[bank] = false;
        PacketManager::free_packet(pkt);
        req_pkt_[bank] = nullptr;
      }
      else
      {
        D_INFO("FeatureBank", "Response failed: bank=%d", bank);
        compute_resp_fifos_[bank].push_back(pkt);
        response_retryResp[bank] = true;
      }
      // if (buffer_is_clear_flag[bank]){
      //   buffer_is_clear_flag[bank] = false;
      //    releaseBankBuffer(bank, getReadableBufferIndex(bank));
      // }
       
    }

    // 检查所有buffer是否清空
    bool all_clear = true;
    for (int i = 0; i < active_banks_; i++)
    {
      if (!buffer_is_clear_flag[i])
      {
        all_clear = false;
        break;
      }
    }

    if (all_clear) {
      D_INFO("FeatureBank", "All buffers clear, release");
      for (int i = 0; i < active_banks_; i++) {
        buffer_is_clear_flag[i] = false;
        releaseBankBuffer(i, getReadableBufferIndex(i));

      }
    }
  }

  Port &FeatureBank::getPort(const std::string &if_name, int idx)
  {
    return DmaBuffer::getPort(if_name, idx);
  }

} // namespace GNN