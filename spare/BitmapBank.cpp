#include "BitmapBank.h"
#include "../common/debug.h"

namespace GNN
{

  BitmapBank::BitmapBank(const std::string& name,
                         addr_t             base_addr,
                         int                burst_num,
                         int                active_banks,
                         uint64_t           inst_burst_num)
    : DmaBuffer(name,
                base_addr,
                burst_num,
                active_banks,
                TOTAL_SLICE_NUM_CFG,
                inst_burst_num,
                "./data/layer0_weight_bitmap_",
                ".txt"),
      inst_burst_num_(inst_burst_num),
      selfScheduleEvent([this] { startNextDmaCommand(); },
                        name + ".SelfScheduleEvent")
  {
    cmd_id_cnts_.resize(active_banks_, 0);
  }

  void BitmapBank::init()
  {
    startNextDmaCommand();
    startNextDmaCommand();
  }

  void BitmapBank::startNextDmaCommand()
  {
    for (int i = 0; i < active_banks_; ++i)
    {
      if (cmd_id_cnts_[i] >= inst_burst_num_)
        continue;
      if (inst_cnts_[i] >= 2)
      {
        D_WARN("BitmapBank", "Bank%d instruction counter full (inst_cnt=2)", i);
        continue;
      }
      addr_t addr =
        base_addr_ + cmd_id_cnts_[i] * INST_ADDR_STRIDE * burst_num_;
      startBitmapLoadCommand(cmd_id_cnts_[i]++, addr, burst_num_, i);
    }
  }

  void BitmapBank::startBitmapLoadCommand(uint64_t cmd_id,
                                          addr_t   base_addr,
                                          int      total_lines,
                                          int      bank_id)
  {
    DmaCommand cmd;
    cmd.bank_id     = bank_id;
    cmd.cmd_id      = cmd_id;
    cmd.base_addr   = base_addr;
    cmd.total_lines = total_lines;
    D_INFO("INST",
           "Bank %d: Start cmd %lu, addr=0x%x, lines=%d  %s",
           bank_id,
           cmd_id,
           base_addr,
           total_lines,
           name().c_str());
    enqueueCommand(cmd);
  }

  void BitmapBank::CompleteCommand(uint64_t bank_id)
  {
    if (cmd_id_cnts_[bank_id] < inst_burst_num_)
    {

      addr_t addr =
        base_addr_ + cmd_id_cnts_[bank_id] * INST_ADDR_STRIDE * burst_num_;
      D_INFO("BitmapBank",
             "Bank %d: Complete cmd %lu, addr=%d, lines=%d,inst_burst_num_=%d",
             bank_id,
             cmd_id_cnts_[bank_id],
             addr,
             burst_num_,
             inst_burst_num_);
      startBitmapLoadCommand(
        cmd_id_cnts_[bank_id]++, addr, burst_num_, bank_id);
    }
  }

  void BitmapBank::sendRespond()
  {
    for (int bank = 0; bank < active_banks_; bank++)
    {
      if (response_retryResp[bank] || !recv_req_send_resp[bank])
        continue;
      int idx = getReadableBufferIndex(bank);
      if (idx < 0)
        continue;
      assert(!bank_controllers_[bank].buffers[idx].dma_pkt.empty());
      auto&     dq  = bank_controllers_[bank].buffers[idx].dma_pkt;
      // if(bank==2)
      // D_INFO("CAM", "dq.size() %d bank %d idx %d next_read_idx_:%d",dq.size(),bank,idx,next_read_idx_[bank]);
      PacketPtr pkt = dq.front();
      assert(pkt != nullptr);
      pkt->setCmdId(buf_cmd_id_[bank][idx]);
      pkt->setBankId(bank);
      pkt->setBufferIdx(idx);

      PacketPtr req_pkt = req_pkt_[bank];
      if (computePorts[bank].sendTimingResp(pkt))
      {
        recv_req_send_resp[bank] = false;
        dq.erase(dq.begin());
        if (dq.empty())
        {
          D_INFO("BitmapBank", "Buffer empty, release bank=%d", bank);
          releaseBankBuffer(bank, idx);
          PacketManager::free_packet(req_pkt);
          req_pkt_[bank] = nullptr;
        }
      }
      else
      {
        D_INFO("BitmapBank", "Response failed: bank=%d", bank);
        compute_resp_fifos_[bank].push_back(pkt);
        response_retryResp[bank] = true;
      }
    }
  }

  Port& BitmapBank::getPort(const std::string& if_name, int idx)
  {
    return DmaBuffer::getPort(if_name, idx);
  }

}  // namespace GNN
