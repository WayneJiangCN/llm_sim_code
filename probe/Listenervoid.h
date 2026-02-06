// #include "common/common.h"
// #include "probe/probe.h"
// #include "Buffer/Buffer.h"
// class DramDataListener : public ProbeListenerArgBase<DramDataPayload> {
// public:
//     DramDataListener(const std::string& name) : ProbeListenerArgBase<DramDataPayload>(name) {}
//     void notify(const DramDataPayload& payload) override {
//         std::cout << "监听到DRAM数据就绪: 地址=" << payload.address << std::endl;
//     }
// };

// class BufferDataListener : public ProbeListenerArgBase<int> {
// public:
//     BufferDataListener(const std::string& name) : ProbeListenerArgBase<int>(name) {}
//     void notify(const int& value) override {
//         std::cout << "监听到Buffer数据就绪: value=" << value << std::endl;
//     }
// };