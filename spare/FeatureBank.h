#ifndef GNN_FEATURE_BANK_H_
#define GNN_FEATURE_BANK_H_

#include "../dma/DmaBuffer.h"
#include "../common/define.h"

namespace GNN {

// 特征银行：存储特征数据
class FeatureBank : public DmaBuffer {
private:
    std::vector<uint64_t> cmd_id_cnts_;
    EventFunctionWrapper selfScheduleEvent;
    std::vector<bool> buffer_is_clear_flag;
    
public:
    FeatureBank(const std::string& name, addr_t base_addr, int burst_num, int active_banks);
    
    void init() override;
    void CompleteCommand(uint64_t bank_id) override;
    void sendRespond() override;
    bool recvTimingReq(PacketPtr pkt, uint32_t bank_id) override;
    Port &getPort(const std::string &if_name, int idx = -1) override;

private:
    void startNextDmaCommand();
    void startFeatureLoadCommand(uint64_t cmd_id, addr_t base_addr, int total_lines, int bank_id);
};

} // namespace GNN

#endif // GNN_FEATURE_BANK_H_