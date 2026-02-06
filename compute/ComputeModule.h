/*
 * 计算模块：作为主控制器，向DmaBuffer下发命令。
 * 通过命令回调得知数据获取完成，然后开始处理。
 */

#ifndef GNN_COMPUTE_MODULE_H_
#define GNN_COMPUTE_MODULE_H_

#include "common/object.h"
#include "event/eventq.h"
#include "common/port.h"
#include "common/packet.h"
#include <string>
#include <vector>
#include <set>

namespace GNN {

class ComputeModule : public SimObject {
public:
    ComputeModule(const std::string &name, int active_banks);

    void init() override;

    // 设置 A 矩阵（1xN 向量）
    void setA(const std::vector<int32_t> &a_vec);

    // 读取最近一次计算的每列输出
    const std::vector<long long> &getOutputs() const;

private:
    int active_banks_;
    std::vector<int32_t> A_;
    int N_ = 0;
    
    std::vector<long long> processed_chunks_per_bank_;
    std::vector<long long> output_per_bank_;

    // 请求状态管理
    std::vector<bool> pending_request_;
    std::vector<bool> in_flight_;

    // 定时事件：驱动请求发送（带退避）
    EventFunctionWrapper requestEvent;
    void requestTick();

   // 计算侧作为请求方，请求每个bank的数据
   class CompRequestPort : public RequestPort {
       ComputeModule &owner;
       int bank_id;
     public:
       CompRequestPort(const std::string &name, ComputeModule &o, int id);
       bool recvTimingResp(PacketPtr pkt) override;
       void recvReqRetry() override;
   };
   std::vector<CompRequestPort> requestPorts;

    bool recvTimingResp(PacketPtr pkt,uint64_t bank_id);

    void scheduleRequestIfNeeded(uint32_t delay);

   // 输出侧作为响应方，外部请求时返回计算结果
   class CompResponsePort : public ResponsePort {
       ComputeModule &owner;
     public:
       CompResponsePort(const std::string &name, ComputeModule &o)
           : ResponsePort(name), owner(o) {}
       bool recvTimingReq(PacketPtr pkt) override;
       void recvRespRetry() override;
   };
   CompResponsePort responsePort;
   PacketPtr pending_resp_ = nullptr;

public:
   Port &getPort(const std::string &if_name, int idx = -1) override;
};

} // namespace GNN

#endif // GNN_COMPUTE_MODULE_H_
 
 