#ifndef GNN_WEIGHT_BANK_H_
#define GNN_WEIGHT_BANK_H_

#include "../dma/DmaBuffer.h"
#include "../common/define.h"

namespace GNN {

// 权重银行：存储权重数据
class WeightBank : public DmaBuffer {
private:
    std::vector<uint64_t> cmd_id_cnts_;
    EventFunctionWrapper selfScheduleEvent;
    
public:
    WeightBank(const std::string& name, addr_t base_addr, int burst_num, int active_banks);
    
    void init() override;
    void CompleteCommand(uint64_t bank_id) override;
    void sendRespond() override;
    Port &getPort(const std::string &if_name, int idx = -1) override;

private:
    void startNextDmaCommand();
    void startWeightLoadCommand(uint64_t cmd_id, addr_t base_addr, int total_lines, int bank_id);
};

} // namespace GNN

#endif // GNN_WEIGHT_BANK_H_