// src/compute/DecoderModule.h

#ifndef GNN_DECODER_MODULE_H_
#define GNN_DECODER_MODULE_H_

#include "buffer/buffer.h"
#include "common/define.h"
#include "common/object.h"
#include "common/packet.h"
#include "common/port.h"
#include "dram/sim_dram_storage.h"
#include "event/eventq.h"
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
namespace GNN
{
  // ===== 矩阵分块计算配置 =====
  struct MatrixBlockConfig
  {
    // 原始矩阵维度
    uint32_t total_input_dim;   // Feature 维度（N）
    uint32_t total_output_dim;  // Weight 行维度（M）

    // SRAM 限制和分块策略
    uint32_t sram_capacity;       // SRAM 最大容量（字数）
    uint32_t feature_block_size;  // Feature 分块大小（固定）
    uint32_t weight_block_rows;   // Weight 每次读取的行数
    uint32_t weight_block_cols;   // Weight 每次读取的列数

    // 计算出的分块信息
    uint32_t total_feature_blocks;  // Feature 需要多少块
    uint32_t total_weight_blocks;   // Weight 需要多少块
    uint32_t final_weight_blocks;   // Weight 最后需要多少块
  };

  // Llama 7B 参数配置表
  struct LayerParamConfig
  {
    // 基本信息
    std::string param_name;
    std::string param_type;  // "attention_qkv", "attention_out", "mlp_gate", "mlp_up", "mlp_down"

    // 矩阵维度
    uint32_t file_total_row;   // 输入维度（Feature 维度）
    uint32_t final_slice_row;  // 最后一个 Feature 切片的行数
    uint32_t file_total_col;   // 输出维度（Weight 维度）

    // 分块配置
    uint32_t file_slice_row;  // 单次读取的 Feature 大小
    uint32_t file_slice_col;  // 单次读取的 Weight 列数

    // 计算分块信息的函数
    MatrixBlockConfig computeBlockConfig(uint32_t sram_capacity) const
    {
      MatrixBlockConfig cfg;
      cfg.total_input_dim    = file_total_row;
      cfg.total_output_dim   = file_total_col;
      cfg.sram_capacity      = sram_capacity;
      cfg.feature_block_size = file_slice_row;  // Feature 固定分块
      cfg.weight_block_rows  = file_slice_row;  // Weight 行与 Feature 匹配
      cfg.weight_block_cols  = file_slice_col;  // Weight 列大小

      // 计算分块数量（使用向上取整）
      // 公式: ceil(a/b) = (a + b - 1) / b
      // 例如: 4100行, 512行/块 -> (4100+512-1)/512 = 4611/512 = 9块
      cfg.total_feature_blocks = (file_total_row + file_slice_row - 1) / file_slice_row;
      cfg.total_weight_blocks =
        (file_total_col + file_slice_col * CHANNEL_NUM - 1) / (file_slice_col * CHANNEL_NUM);

      return cfg;
    }
  };

  // Llama 7B 所有参数配置表（按计算顺序）
  static const std::vector<LayerParamConfig> LLAMA_7B_PARAMS = {
    // Layer 0
    // Attention: Q(4096x4096), K(4096x1024), V(4096x1024), Out(4096x4096)

    { "layer_0_mlp_down", "mlp_down", 11008, 2816, 4096, 4096, BITMAP_WORD_BITS },

    // MLP: Gate(4096x11008), Up(4096x11008), Down(11008x4096)
    { "layer_0_mlp_gate", "mlp_gate", 4096, 0, 11008, 4096, BITMAP_WORD_BITS },

    { "layer_0_attention_k", "attention_qkv", 4096, 0, 4096, 4096, BITMAP_WORD_BITS },
    { "layer_0_attention_out", "attention_out", 4096, 0, 4096, 4096, BITMAP_WORD_BITS },
    { "layer_0_attention_q", "attention_qkv", 4096, 0, 4096, 4096, BITMAP_WORD_BITS },

    { "layer_0_mlp_up", "mlp_up", 4096, 0, 11008, 4096, BITMAP_WORD_BITS },
    { "layer_0_attention_v", "attention_qkv", 4096, 0, 4096, 4096, BITMAP_WORD_BITS },
    { "layer_1_mlp_down", "mlp_down", 11008, 2816, 4096, 4096, BITMAP_WORD_BITS },

    // MLP: Gate(4096x11008), Up(4096x11008), Down(11008x4096)
    { "layer_1_mlp_gate", "mlp_gate", 4096, 0, 11008, 4096, BITMAP_WORD_BITS },

    { "layer_1_attention_k", "attention_qkv", 4096, 0, 4096, 4096, BITMAP_WORD_BITS },
    { "layer_1_attention_out", "attention_out", 4096, 0, 4096, 4096, BITMAP_WORD_BITS },
    { "layer_1_attention_q", "attention_qkv", 4096, 0, 4096, 4096, BITMAP_WORD_BITS },

    { "layer_1_mlp_up", "mlp_up", 4096, 0, 11008, 4096, BITMAP_WORD_BITS },
    { "layer_1_attention_v", "attention_qkv", 4096, 0, 4096, 4096, BITMAP_WORD_BITS }

    // Layer 1 (重复模式，此处省略细节)
    // ... 重复 31 次 (共32层)

    // Output Layer
    // {
    //     "lm_head", "output",
    //     4096, 32000, 512, 4000
    // }
  };

  // ===== 计算状态跟踪 =====
  struct ComputeBlockState
  {
    // 当前分块位置
    uint32_t current_feature_block = 0;  // 当前 Feature 块索引
    uint32_t current_weight_block  = 0;  // 当前 Weight 块索引
    uint32_t total_feature_blocks  = 0;
    uint32_t total_weight_blocks   = 0;

    // Feature 流动状态
    bool     feature_loaded    = false;  // Feature 是否已加载到 SRAM
    uint32_t feature_rows_read = 0;      // 已读取的 Feature 行数

    // Weight 流动状态
    uint32_t weight_cols_read        = 0;  // 已读取的 Weight 列数
    uint32_t weight_rows_accumulated = 0;  // 累积的 Weight 行数

    // 清空状态
    void reset()
    {
      current_feature_block   = 0;
      current_weight_block    = 0;
      feature_loaded          = false;
      feature_rows_read       = 0;
      weight_cols_read        = 0;
      weight_rows_accumulated = 0;
    }

    // 检查是否需要加载下一个 Weight 块
    bool needNextWeightBlock() const { return current_weight_block < total_weight_blocks; }

    // 检查是否完成了当前 Feature 块的所有 Weight 计算
    bool featureBlockComplete() const { return current_weight_block >= total_weight_blocks; }

    // 推进到下一个 Feature 块
    void advanceFeatureBlock()
    {
      current_feature_block++;
      current_weight_block = 0;  // Weight 块重置
      feature_loaded       = false;
      weight_cols_read     = 0;
    }
  };

  // Decoder 内部状态机，用于控制 HxW 块的处理流程
  enum class DecoderState
  {
    IDLE,          // 等待开始新的 HxW 块处理
    BITMAP_WAIT,   // 已发送 Bitmap 请求，等待响应
    DECODE_START,  // 已收到 Bitmap，正在准备 Weight/Feature 地址
    WF_WAIT,       // 已发送 Weight/Feature 请求，等待所有响应
    OUTPUT_READY   // 数据已准备好发送到下一级（例如 CAM）
  };

  // 用于保存解码后的稀疏性信息和地址（每个 Bank/块独立）
  struct DecodedBlockInfo
  {
    uint64_t current_cmd_id     = 0;   // 当前正在处理的 DmaCommand ID
    size_t   total_elements     = 0;   // 从 Bitmap 解压得到的总元素数量
    size_t   processed_count    = 0;   // 已经处理的非零元素数量
    size_t   elements_per_cycle = 32;  // 每个cycle处理32个元素

    size_t                ones_in_bitmap = 0;        // 当前bitmap中1的数量（用于CAM与流程控制）
    PacketPtr             bitmap_pkt     = nullptr;  // 保存bitmap响应包，处理完成后释放
    // 按行统计：bitmap可能是任意bits（如18bit）
    std::vector<size_t>   row_ones_counts;      // 每行1的数量
    std::vector<bitmap_t> row_bits_data;        // 每行的原始bitmap bits
    size_t                total_rows      = 0;  // 本bitmap覆盖的总行数
    // 以32-bit为周期单位
    size_t                total_words     = 0;  // bitmap包含的32-bit word数量
    size_t                processed_words = 0;  // 已处理的word数量

    // 每个bank独立的状态机
    DecoderState state = DecoderState::IDLE;

    // 状态标记
    bool weight_received            = false;
    bool feature_received           = false;
    bool bitmap_processing_complete = false;  // bitmap数据处理是否完成
  };
  struct WFRequestInfo
  {
    size_t read4weight_nums = 0;      //  读weight的数量
    size_t read4row_nums    = 0;      //  读row的数量
    bool   feature_is_clear = false;  // 特征缓冲区是否为空
    bool   weight_is_empty  = false;
  };
  struct File_Info
  {
    uint16_t channel_id         = 0;
    uint32_t file_total_row     = 0;
    uint32_t file_total_col     = 0;
    uint32_t file_slice_row     = 0;
    uint32_t file_slice_col     = 0;
    uint32_t final_slice_row    = 0;
    uint32_t total_addr_count   = 0;
    uint32_t final_addr         = 0;
    uint32_t current_addr_count = 0;
    bool     decoder_stall      = false;
    bool     add_stall          = false;
  };

  class DecoderModule : public SimObject
  {
  public:
    DecoderModule(const std::string& name,
                  int                active_banks,
                  SimDramStorage*    sim_dram_storage,
                  Buffer*            write_buffer);

    SimDramStorage* sim_dram_storage_;
    void            init() override;

    // 设置下一个 HxW 块的起始命令 (由 Sparsity Scheduler 触发)
    void                   startNewBlock(uint64_t cmd_id, address_t base_addr);
    std::vector<File_Info> file_stall;

  private:
    std::ofstream                                  OUT;
    std::ofstream                                  OUT3;
    std::ofstream                                  OUT1;
    std::ofstream                                  OUT2;
    int                                            active_banks_;
    std::vector<bool>                              weight_success;
    std::vector<std::vector<std::deque<uint32_t>>> adder_fifos;
    Buffer*                                        write_buffer_;
    std::vector<bool>                              feature_success;
    // 每个 Bank 的状态和数据信息
    std::vector<DecodedBlockInfo>                  bank_states_;
    std::vector<WFRequestInfo>                     bank_wf_request_info_;
    std::vector<addr_t>                            next_write_addr_;
    std::vector<uint64_t>                          add_stall_cycle_;

    // 每个Bank的Hash CAM（最多64个槽位）
    struct CamEntry
    {
      int      value;
      bitmap_t row_bits;
    };
    struct CamBank
    {
      // 按值桶存储，便于O(1)查找配对（平均）
      std::unordered_map<int, std::deque<CamEntry>> buckets;
      size_t                                        size = 0;
    };
    struct Retry2CamInfo
    {
      bool     retry2Cam_flag = false;
      CamEntry entry[8];  // 8 entries: 2 rows × 4 segments
      bool     paired_success[8] = { false, false, false, false, false, false, false, false };
    };
    std::vector<Retry2CamInfo>          Info2Cam_;
    // Eight CAMs per bank: 2 rows × 4 segments per row
    // Access: hash_cam_[bank_id][row * 4 + segment]
    std::vector<std::array<CamBank, 8>> hash_cam_;
    static constexpr int                Pairing_value        = 32;
    static constexpr int                kHashCamCapacity     = 64;
    static constexpr int                kAggressiveThreshold = 59;

    // Hash CAM性能统计（每个bank独立）
    struct HashCamPerfStats
    {
      uint64_t current_rd_addr            = 0;  // 当前读地址
      uint64_t total_cycles               = 0;  // 总cycle数
      uint64_t cam_full_cycles            = 0;  // CAM满的cycle数
      uint64_t emit_single_cycles         = 0;  // 一次只出一个数的cycle数
      uint64_t emit_paired_full_cycles    = 0;  // 一次出两个数的cycle数
      uint64_t emit_paired_disfull_cycles = 0;  // 一次出两个数的cycle数
    };
    std::vector<HashCamPerfStats> hash_cam_perf_stats_;
    uint64_t                      totall_num_output = 0;
    uint64_t                      totall_num_input  = 0;
    std::array<uint64_t, 16 + 1>  emitted0_hist_{};
    uint64_t                      current_addr;
    // 将每个cycle的两行(值+原始16bit)送入对应bank的Hash CAM进行配对与输出
    bool                          processHashCam(uint32_t bank_id);
    // 尝试把一个值插入并与已有值配对（配对和为16则输出），返回是否已配对
    bool CamalfullAndPair(uint32_t bank_id, int cam_idx, int value, bitmap_t row_bits);
    bool tryInsertAndPair(uint32_t bank_id, int cam_idx, int value, bitmap_t row_bits);
    // 仅插入值，不尝试配对（用于一次只输出一对的场景）
    bool tryInsertOnly(uint32_t bank_id, int cam_idx, int value, bitmap_t row_bits);
    // 快满时查找并输出两个值之和小于16的配对（用于释放空间）
    bool evictPairsLessThan16(uint32_t bank_id, int cam_idx);
    // 成功配对输出（当前实现仅记录与占位，后续可接下游）
    void
    emitPaired(uint32_t bank_id, int a, int b, bitmap_t rowA_bits, bitmap_t rowB_bits, int cam_idx);
    // 单独输出一个值（找不到配对时）
    void emitSingle(uint32_t bank_id, int a, bitmap_t rowA_bits, int cam_idx);
    // 刷新：在一个bitmap处理完后，将剩余未配对项按“两行两行”输出
    void flushCam(uint32_t bank_id);

    void bitonic_merge(uint16_t bank_id, bitmap_t a, bitmap_t b, bitmap_t rowA, bitmap_t rowB);
    void bitmapToIdxVec(bitmap_t bits, uint32_t wordBits, std::vector<uint32_t>& out);
    void bitonicMergeAsc(std::vector<uint32_t>& v);

    // 请求状态管理
    std::vector<bool>    pending_request_;          // 标记该 bank 是否有待请求
    std::vector<bool>    pending_weight_request_;   // 标记该 bank 是否有待权重请求
    std::vector<bool>    pending_feature_request_;  // 标记该 bank 是否有待特征请求
    std::vector<bool>    in_flight_;                // 标记请求是否已发出，等待响应
    std::vector<bool>    weight_in_flight_;         // 标记权重请求是否已发出，等待响应
    std::vector<bool>    feature_in_flight_;        // 标记特征请求是否已发出，等待响应
    // 定时事件：驱动请求发送和状态机 Tick
    EventFunctionWrapper tickEvent;
    EventFunctionWrapper retry2CamEvent;
    EventFunctionWrapper clearCamEvent;
    void                 retry2CamTick();
    void                 scheduleRetry2CamIfNeeded(uint32_t delay);
    void                 tick();
    void                 scheduleTickIfNeeded(uint32_t delay);
    void                 scheduleClearCamTick(uint32_t delay);
    void                 clearCamtick();
    void sendResultToBuffer(uint32_t bank_id, const std::vector<storage_t>& payload);

    // ========= 请求端口定义 (向 Banks 拉取数据) =========

    class BankRequestPort : public RequestPort
    {
      DecoderModule& owner;
      int            bank_id;
      std::string    bank_name;  // 标记端口类型：bmap, w, f
    public:
      BankRequestPort(const std::string& name, DecoderModule& o, int id, const std::string& type);
      bool recvTimingResp(PacketPtr pkt) override;
      void recvReqRetry() override;
    };

    // 端口实例：每个 bank 有 3 个请求端口
    std::vector<BankRequestPort> bitmapRequestPorts;
    std::vector<BankRequestPort> weightRequestPorts;
    std::vector<BankRequestPort> featureRequestPorts;

    // ========= 响应端口定义 (向 CAM/下一级推送数据) =========
    class CompResponsePort : public ResponsePort
    {
      DecoderModule& owner;
      int            bank_id;

    public:
      CompResponsePort(const std::string& name, DecoderModule& o, int id);
      bool recvTimingReq(PacketPtr pkt) { return false; };
      bool recvTimingResp(PacketPtr pkt, int port_id) { return false; };
      void recvRespRetry() { return; };
    };
    std::vector<CompResponsePort> computresponsePort;

    // 统一的响应接收函数，根据端口类型分派
    bool recvTimingResp(PacketPtr pkt, uint32_t bank_id, const std::string& bank_name);

    // 核心处理函数
    void handleBitmapResponse(uint32_t bank_id, PacketPtr pkt);
    void handleWFResponse(uint32_t bank_id, PacketPtr pkt, bool is_weight);
    void attemptNextRequest(uint32_t bank_id);
    bool checkBlockCompletion(uint32_t bank_id);
    void resetBankState(int bank_id);
    void driveCamOnce(int bank_id);
    bool camHasPendingData(int bank_id) const;

  public:
    Port& getPort(const std::string& if_name, int idx = -1) override;
    void  printHashCamStats() const;
    void  exportHashCamStats(const std::string& filename) const;
    void  printHashCamPerfStats(uint32_t bank_id);
    void  exportHashCamPerfStats(const std::string& filename);

  private:
    // ===== 参数轮询相关 =====
    size_t current_param_idx_ = 0;
    size_t total_params_      = LLAMA_7B_PARAMS.size();

    // ===== 计算状态管理（每个 Bank） =====
    std::vector<ComputeBlockState> compute_block_states_;
    std::vector<MatrixBlockConfig> current_block_configs_;

    // 获取当前参数配置
    const LayerParamConfig& getCurrentParamConfig() const
    {
      if (current_param_idx_ >= LLAMA_7B_PARAMS.size())
      {
        return LLAMA_7B_PARAMS.back();
      }
      return LLAMA_7B_PARAMS[current_param_idx_];
    }

    // 轮询到下一个参数（全局轮询，只调用一次）
    void advanceToNextParam()
    {
      if (current_param_idx_ < LLAMA_7B_PARAMS.size() - 1)
      {
        current_param_idx_++;
        D_DEBUG("BLOCK",
                "Advancing to next parameter: %s (idx=%zu/%zu)",
                getCurrentParamConfig().param_name.c_str(),
                current_param_idx_,
                total_params_);
        // 重置所有 bank 的计算状态
        resetAllComputeStates();
      }
      else
      {
        D_DEBUG("BLOCK", "All parameters processed! (total=%zu)", total_params_);
      }
    }

    // 检查是否所有 Bank 都完成了当前参数
    bool allBanksCompleteCurrentParam() const
    {
      for (size_t i = 0; i < file_stall.size(); ++i)
      {
        if (!file_stall[i].decoder_stall)
        {
          return false;  // 至少有一个 Bank 还未完成
        }
      }
      return true;  // 所有 Bank 都完成了
    }

    // 重置计算状态
    void resetAllComputeStates()
    {
      const auto& param = getCurrentParamConfig();
      for (size_t i = 0; i < compute_block_states_.size(); ++i)
      {
        compute_block_states_[i].reset();
        current_block_configs_[i] = param.computeBlockConfig(SRAM_CAPACITY);
        compute_block_states_[i].total_feature_blocks =
          current_block_configs_[i].total_feature_blocks;
        compute_block_states_[i].total_weight_blocks =
          current_block_configs_[i].total_weight_blocks;
      }
    }

    // 重置所有 Bank 的 decoder_stall 标志
    void resetAllBankStallFlags()
    {
      for (size_t i = 0; i < file_stall.size(); ++i)
      {
        file_stall[i].decoder_stall      = false;
        file_stall[i].current_addr_count = 0;
      }
    }
  };

}  // namespace GNN

#endif  // GNN_DECODER_MODULE_H_