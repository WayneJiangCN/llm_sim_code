// // src/compute/DecoderModule.h

// #ifndef GNN_DECODER_MODULE_H_
// #define GNN_DECODER_MODULE_H_

// #include "common/object.h"
// #include "event/eventq.h"
// #include "common/port.h"
// #include "dram/sim_dram_storage.h"
// #include "common/packet.h"
// #include "common/define.h"
// #include <cstdint>
// #include <string>
// #include <vector>
// #include <map>
// #include <unordered_map>
// #include <deque>
// #include <array>
// #include <iostream>
// #include "buffer/buffer.h"
// namespace GNN
// {

//     // Decoder 内部状态机，用于控制 HxW 块的处理流程
//     enum class DecoderState
//     {
//         IDLE,         // 等待开始新的 HxW 块处理
//         BITMAP_WAIT,  // 已发送 Bitmap 请求，等待响应
//         DECODE_START, // 已收到 Bitmap，正在准备 Weight/Feature 地址
//         WF_WAIT,      // 已发送 Weight/Feature 请求，等待所有响应
//         OUTPUT_READY  // 数据已准备好发送到下一级（例如 CAM）
//     };

//     // 用于保存解码后的稀疏性信息和地址（每个 Bank/块独立）
//     struct DecodedBlockInfo
//     {
//         uint64_t current_cmd_id = 0;    // 当前正在处理的 DmaCommand ID
//         size_t total_elements = 0;      // 从 Bitmap 解压得到的总元素数量
//         size_t processed_count = 0;     // 已经处理的非零元素数量
//         size_t elements_per_cycle = 32; // 每个cycle处理32个元素

//         size_t ones_in_bitmap = 0;      // 当前bitmap中1的数量（用于CAM与流程控制）
//         PacketPtr bitmap_pkt = nullptr; // 保存bitmap响应包，处理完成后释放
//         // 按行统计：bitmap可能是任意bits（如18bit）
//         std::vector<size_t> row_ones_counts; // 每行1的数量
//         std::vector<bitmap_t> row_bits_data; // 每行的原始bitmap bits
//         size_t total_rows = 0;               // 本bitmap覆盖的总行数
//         // 以32-bit为周期单位
//         size_t total_words = 0;     // bitmap包含的32-bit word数量
//         size_t processed_words = 0; // 已处理的word数量

//         // 每个bank独立的状态机
//         DecoderState state = DecoderState::IDLE;

//         // 状态标记
//         bool weight_received = false;
//         bool feature_received = false;
//         bool bitmap_processing_complete = false; // bitmap数据处理是否完成
//     };
//     struct WFRequestInfo
//     {
//         size_t read4weight_nums = 0;   //  读weight的数量
//         size_t read4row_nums = 0;      //  读row的数量
//         bool feature_is_clear = false; // 特征缓冲区是否为空
//         bool weight_is_empty = false;
//     };
//     struct File_Info
//     {
//         uint16_t channel_id = 0;
//         uint32_t file_total_row = 0;
//         uint32_t file_total_col = 0;
//         uint32_t file_slice_row = 0;
//         uint32_t file_slice_col = 0;
//         uint32_t total_addr_count = 0;
//         uint32_t final_addr = 0;
//         uint32_t current_addr_count = 0;
//         bool decoder_stall = false;
//         bool add_stall = false;
//     };

//     class DecoderModule : public SimObject
//     {
//     public:
//         DecoderModule(const std::string &name, int active_banks, SimDramStorage *sim_dram_storage, Buffer *write_buffer);

//         SimDramStorage *sim_dram_storage_;
//         void init() override;

//         // 设置下一个 HxW 块的起始命令 (由 Sparsity Scheduler 触发)
//         void startNewBlock(uint64_t cmd_id, address_t base_addr);
//         std::vector<File_Info> file_stall;

//     private:
//         std::ofstream OUT;
//         std::ofstream OUT3;
//         std::ofstream OUT1;
//         std::ofstream OUT2;
//         int active_banks_;
//         std::vector<bool> weight_success;
//         std::vector<std::vector<std::deque<uint32_t>>> adder_fifos;
//         Buffer *write_buffer_;
//         std::vector<bool> feature_success;
//         // 每个 Bank 的状态和数据信息
//         std::vector<DecodedBlockInfo> bank_states_;
//         std::vector<WFRequestInfo> bank_wf_request_info_;
//         std::vector<addr_t> next_write_addr_;
//          std::vector<uint64_t> add_stall_cycle_;

//         // 每个Bank的Hash CAM（最多64个槽位）
//         struct CamEntry
//         {
//             int value;
//              bitmap_t row_bits;
//         };
//         struct CamBank
//         {
//             // 按值桶存储，便于O(1)查找配对（平均）
//             std::unordered_map<int, std::deque<CamEntry>> buckets;
//             size_t size = 0;
//         };
//         struct Retry2CamInfo
//         {
//             bool retry2Cam_flag = false;
//             CamEntry entry[2];
//             bool paired_success[2] = {false, false};
//         };
//         std::vector<Retry2CamInfo> Info2Cam_;
//         // Two CAMs per bank: index 0 for row0, index 1 for row1
//         std::vector<std::array<CamBank, 2>> hash_cam_;
//         static constexpr int kHashCamCapacity = 64;
//         static constexpr int kAggressiveThreshold = 62;

//         // Hash CAM性能统计（每个bank独立）
//         struct HashCamPerfStats
//         {
//             uint64_t total_cycles = 0;       // 总cycle数
//             uint64_t cam_full_cycles = 0;    // CAM满的cycle数
//             uint64_t emit_single_cycles = 0; // 一次只出一个数的cycle数
//             uint64_t emit_paired_cycles = 0; // 一次出两个数的cycle数
//         };
//         std::vector<HashCamPerfStats> hash_cam_perf_stats_;
//         uint64_t totall_num_output = 0;
//         uint64_t totall_num_input = 0;
//         std::array<uint64_t, BITMAP_WORD_BITS+1> emitted0_hist_{};
//         uint64_t current_addr;
//         // 将每个cycle的两行(值+原始16bit)送入对应bank的Hash CAM进行配对与输出
//         bool processHashCam(uint32_t bank_id);
//         // 尝试把一个值插入并与已有值配对（配对和为16则输出），返回是否已配对
//         bool tryInsertAndPair(uint32_t bank_id, int cam_idx, int value, bitmap_t row_bits);
//         // 仅插入值，不尝试配对（用于一次只输出一对的场景）
//         bool tryInsertOnly(uint32_t bank_id, int cam_idx, int value, bitmap_t row_bits);
//         // 快满时查找并输出两个值之和小于16的配对（用于释放空间）
//         bool evictPairsLessThan16(uint32_t bank_id, int cam_idx);
//         // 成功配对输出（当前实现仅记录与占位，后续可接下游）
//         void emitPaired(uint32_t bank_id, int a, int b, bitmap_t rowA_bits, bitmap_t rowB_bits, int cam_idx);
//         // 单独输出一个值（找不到配对时）
//         void emitSingle(uint32_t bank_id, int a, bitmap_t rowA_bits, int cam_idx);
//         // 刷新：在一个bitmap处理完后，将剩余未配对项按“两行两行”输出
//         void flushCam(uint32_t bank_id);

//         void bitonic_merge(uint16_t bank_id, bitmap_t a,bitmap_t b,bitmap_t rowA  ,bitmap_t rowB);
//         void bitmapToIdxVec(bitmap_t bits, uint32_t wordBits, std::vector<uint32_t>& out);
//         void bitonicMergeAsc(std::vector<uint32_t>& v);

//         // 请求状态管理
//         std::vector<bool> pending_request_;         // 标记该 bank 是否有待请求
//         std::vector<bool> pending_weight_request_;  // 标记该 bank 是否有待权重请求
//         std::vector<bool> pending_feature_request_; // 标记该 bank 是否有待特征请求
//         std::vector<bool> in_flight_;               // 标记请求是否已发出，等待响应
//         std::vector<bool> weight_in_flight_;        // 标记权重请求是否已发出，等待响应
//         std::vector<bool> feature_in_flight_;       // 标记特征请求是否已发出，等待响应
//         // 定时事件：驱动请求发送和状态机 Tick
//         EventFunctionWrapper tickEvent;
//         EventFunctionWrapper retry2CamEvent;
//         EventFunctionWrapper clearCamEvent;
//         void retry2CamTick();
//         void scheduleRetry2CamIfNeeded(uint32_t delay);
//         void tick();
//         void scheduleTickIfNeeded(uint32_t delay);
//         void scheduleClearCamTick(uint32_t delay);
//         void clearCamtick();
//         void sendResultToBuffer(uint32_t bank_id, const std::vector<storage_t> &payload);

//         // ========= 请求端口定义 (向 Banks 拉取数据) =========

//         class BankRequestPort : public RequestPort
//         {
//             DecoderModule &owner;
//             int bank_id;
//             std::string bank_name; // 标记端口类型：bmap, w, f
//         public:
//             BankRequestPort(const std::string &name, DecoderModule &o, int id, const std::string &type);
//             bool recvTimingResp(PacketPtr pkt) override;
//             void recvReqRetry() override;
//         };

//         // 端口实例：每个 bank 有 3 个请求端口
//         std::vector<BankRequestPort> bitmapRequestPorts;
//         std::vector<BankRequestPort> weightRequestPorts;
//         std::vector<BankRequestPort> featureRequestPorts;

//         // ========= 响应端口定义 (向 CAM/下一级推送数据) =========
//         class CompResponsePort : public ResponsePort
//         {
//             DecoderModule &owner;
//             int bank_id;

//         public:
//             CompResponsePort(const std::string &name, DecoderModule &o, int id);
//             bool recvTimingReq(PacketPtr pkt) { return false; };
//             bool recvTimingResp(PacketPtr pkt, int port_id) { return false; };
//             void recvRespRetry() { return; };
//         };
//         std::vector<CompResponsePort> computresponsePort;

//         // 统一的响应接收函数，根据端口类型分派
//         bool recvTimingResp(PacketPtr pkt, uint32_t bank_id, const std::string &bank_name);

//         // 核心处理函数
//         void handleBitmapResponse(uint32_t bank_id, PacketPtr pkt);
//         void handleWFResponse(uint32_t bank_id, PacketPtr pkt, bool is_weight);
//         void attemptNextRequest(uint32_t bank_id);
//         bool checkBlockCompletion(uint32_t bank_id);
//         void resetBankState(int bank_id);
//         void driveCamOnce(int bank_id);
//         bool camHasPendingData(int bank_id) const;

//     public:
//         Port &getPort(const std::string &if_name, int idx = -1) override;
//         void printHashCamStats() const;
//         void exportHashCamStats(const std::string &filename) const;
//         void printHashCamPerfStats(uint32_t bank_id) ;
//         void exportHashCamPerfStats(const std::string &filename) ;
//     };

// } // namespace GNN

// #endif // GNN_DECODER_MODULE_H_