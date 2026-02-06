/*
 * @Author: wayne 1448119477@qq.com
 * @Date: 2025-11-12 13:19:14
 * @LastEditors: wayne 1448119477@qq.com
 * @LastEditTime: 2025-11-14 14:57:56
 * @FilePath: /simulator_simple/src/spare/WeightBank.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "WeightBank.h"
#include "../common/debug.h"

namespace GNN {

WeightBank::WeightBank(const std::string &name, addr_t base_addr, int burst_num,
                       int active_banks)
    : DmaBuffer(name, base_addr, burst_num, active_banks, 2, 2, 
                "./data/layer0_weight_", ".txt"),
      selfScheduleEvent([this] { startNextDmaCommand(); }, 
                        name + ".SelfScheduleEvent") {
  cmd_id_cnts_.resize(active_banks_, 0);
}

void WeightBank::init() {
  startNextDmaCommand();
  startNextDmaCommand();
}

void WeightBank::startNextDmaCommand() {
  for (int i = 0; i < active_banks_; ++i) {
    if (inst_cnts_[i] >= 2) {
      D_WARN("WeightBank", "Bank%d instruction counter full (inst_cnt=2)", i);
      continue;
    }
    addr_t addr = base_addr_ + cmd_id_cnts_[i] * INST_ADDR_STRIDE * burst_num_;
    startWeightLoadCommand(cmd_id_cnts_[i]++, addr, burst_num_, i);
  }
}

void WeightBank::startWeightLoadCommand(uint64_t cmd_id, addr_t base_addr, 
                                        int total_lines, int bank_id) {
  DmaCommand cmd;
  cmd.bank_id = bank_id;
  cmd.cmd_id = cmd_id;
  cmd.base_addr = base_addr;
  cmd.total_lines = total_lines;
  D_INFO("INST", "Bank %d: Start cmd %lu, addr=0x%x, lines=%d  %s", 
         bank_id, cmd_id, base_addr, total_lines,name().c_str());
  enqueueCommand(cmd);
}

void WeightBank::CompleteCommand(uint64_t bank_id) {
  // if (cmd_id_cnts_[bank_id] < TOTAL_INST_NUM_CFG * 100) {
    addr_t addr = base_addr_ + cmd_id_cnts_[bank_id] * INST_ADDR_STRIDE * burst_num_;
    startWeightLoadCommand(cmd_id_cnts_[bank_id]++, addr, burst_num_, bank_id);
  // }
}

void WeightBank::sendRespond() {
  for (int bank = 0; bank < active_banks_; bank++) {
    if (response_retryResp[bank] || !recv_req_send_resp[bank]) continue;
    int idx = getReadableBufferIndex(bank);
    if (idx < 0) continue;
    
    assert(!bank_controllers_[bank].buffers[idx].dma_pkt.empty());
    
    PacketPtr pkt = req_pkt_[bank];
    if (computePorts[bank].sendTimingResp(pkt)) {
      recv_req_send_resp[bank] = false;
      if (pkt->getWeightBufferIsClear()) {
        D_DEBUG("WeightBank", "Buffer clear, release bank=%d", bank);
        releaseBankBuffer(bank, idx);
      }
      PacketManager::free_packet(pkt);
      req_pkt_[bank] = nullptr;
    } else {
      D_INFO("WeightBank", "Response failed: bank=%d", bank);
      compute_resp_fifos_[bank].push_back(pkt);
      response_retryResp[bank] = true;
    }
  }
}

Port &WeightBank::getPort(const std::string &if_name, int idx) {
  return DmaBuffer::getPort(if_name, idx);
}

} // namespace GNN