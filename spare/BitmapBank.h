#ifndef GNN_SPARSE_BANKS_H_
#define GNN_SPARSE_BANKS_H_

#include "../dma/DmaBuffer.h"
#include "../common/define.h"

namespace GNN {

// 位图银行：存储稀疏位图数据
class BitmapBank : public DmaBuffer {
private:
    std::vector<uint64_t> cmd_id_cnts_;
    EventFunctionWrapper selfScheduleEvent;
    uint64_t inst_burst_num_;
public:
    BitmapBank(const std::string& name, addr_t base_addr, int burst_num, int active_banks, uint64_t inst_burst_num);
    
    void init() override;
    void CompleteCommand(uint64_t bank_id) override;
    void sendRespond() override;
    Port &getPort(const std::string &if_name, int idx = -1) override;

private:
    void startNextDmaCommand();
    void startBitmapLoadCommand(uint64_t cmd_id, addr_t base_addr, int total_lines, int bank_id);
};

} // namespace GNN

#endif // GNN_SPARSE_BANKS_H_