/*
 * 基于命令队列的、自动控制双缓冲的 DMA 上游模块。
 * 作为一个忠实的指令执行引擎，处理来自主控的DMA命令。
 */

 #ifndef GNN_DMABUFFER_H_
 #define GNN_DMABUFFER_H_
 
#include "common/object.h"
#include "common/packet.h"
#include "common/port.h"
#include "event/eventq.h"
#include "common/define.h"
#include "common/file_read.h"
#include <deque>
#include <string>
#include <vector>
#include <cassert>
#include <functional> // 用于 std::function
 
 namespace GNN
 {
 
   // --- FORWARD DECL --
   enum class BufferState { FILLING, FULL };
   struct BankBuffer {
     std::vector<PacketPtr> dma_pkt;
     uint32_t pkt_num;
     BufferState state = BufferState::FILLING;
     int words_written = 0;
   };
   struct BankController {
     BankBuffer buffers[2]; // 0=Ping, 1=Pong
     addr_t dram_base_addr[2];
     addr_t dram_final_addr[2];
     int current_write_idx = 0;
     bool stalled[2] = {false, false};
   };
   enum TransState {
     IDLE,
     CONFIG,
     STREAMING
   };
 
   class DmaBuffer : public SimObject, public FileReader
   {
   public:
     static constexpr int num_ports = 8;
     static constexpr uint32_t addr_stride = 64;
     
 
     // --- 命令结构体定义 ---
     struct DmaCommand
     {
      int bank_id; // 新增，指令专属bank
      addr_t base_addr;
      int total_lines;
      uint64_t cmd_id; // 用于跟踪命令的唯一ID
      // 命令完成回调：当DMA完成数据 *获取* 后调用
      std::function<void(uint64_t cmd_id)> completion_callback;
     };
 
     DmaBuffer(const std::string &name, addr_t base_addr, int burst_num,
               int active_banks = num_ports, uint32_t total_slice_num_cfg = 1, uint32_t total_inst_num_cfg = 1, const std::string& data_file_base_path = "./data/", const std::string& data_file_suffix = ".txt");
 /**
burst_num_ 是多少个Burst
 */
     void init() override;
 
     // --- 端口 API ---
     bool recvTimingResp(PacketPtr pkt, int port_id);
     void sendRetryReq(int port_id);
     Port &getPort(const std::string &if_name, int idx = -1) override;
 
     // --- 核心控制 API ---
     // API: 主控调用此函数将一个DMA任务入队
     void enqueueCommand(const DmaCommand &cmd);
 
     // API: 下游模块释放一个已消耗的缓冲区
     void releaseBankBuffer(int bank_id, int buffer_idx);
 
     // API: 查询数据
     int getReadableBufferIndex(int bank_id) const;
 
   

     virtual void CompleteCommand(uint64_t bank_id) =0 ; // 纯虚函数

     addr_t base_addr_;
 
   public:
 
 
     // --- 多bank独立控制的缓冲与命令 ---
     std::vector<std::deque<DmaCommand>> cmd_queues_; // 每个bank独立命令队列
     std::vector<DmaCommand> current_cmds_; // 每个bank当前命令
     std::vector<int> inst_cnts_; // 每个bank的活跃指令计数
     std::vector<TransState> trans_states_; // 每个bank的状态机状态
     std::vector<int> lines_fetched_for_cmds_; // 每个bank搬运进度
     std::vector<BankController> bank_controllers_;
     // 移除原有全局唯一命令队列/状态/计数等（已在上方替代）
     // std::deque<DmaCommand> cmd_queue_;
     // DmaCommand current_cmd_;
     // int inst_cnt; 
     // TransState trans_state_;
     // int lines_fetched_for_cmd_ = 0;
     const int burst_num_;
     const int active_banks_;
     addr_t bank_rd_addr_[num_ports]{};
     bool bank_transfer_active_[num_ports]{};
     uint32_t addr_stride_;
    
     // 当前命令使用的buf索引 (0 或 1)
     bool current_buf_idx_[num_ports] ;
     // 每个buf对应的命令ID，用于正确识别数据来源
     uint64_t buf_cmd_id_[num_ports][2] = {{0, 0},{0, 0},{0, 0},{0, 0},{0, 0},{0, 0},{0, 0},{0, 0}};
     // 每个buf对应的命令回调函数
     std::function<void(uint64_t)> buf_cmd_callback_[num_ports][2] = {{nullptr, nullptr},{nullptr, nullptr},{nullptr, nullptr},{nullptr, nullptr},{nullptr, nullptr},{nullptr, nullptr},{nullptr, nullptr},{nullptr, nullptr}};
 
     // --- 内部资源 ---
  
   
     std::vector<std::deque<PacketPtr>> req_fifos_;
     PacketPtr req_pkt_[num_ports];
     class DmaRequestPort : public RequestPort
     {
       DmaBuffer &owner;
       int port_id;
 
     public:
       DmaRequestPort(const std::string &name, DmaBuffer &o, int id);
       bool recvTimingResp(PacketPtr pkt) override;
       
       void recvReqRetry() override;
     };
     std::vector<DmaRequestPort> requestPorts;
     EventFunctionWrapper sendRespondEvent;
     // 计算侧端口（响应端），供计算模块作为请求端发起拉取
     class ComputeSidePort : public ResponsePort
     {
       DmaBuffer &owner;
       int bank_id;
 
     public:
       ComputeSidePort(const std::string &name, DmaBuffer &o, int id);
       bool recvTimingReq(PacketPtr pkt) { return owner.recvTimingReq(pkt, bank_id); };
       bool tryTiming(PacketPtr pkt) override;
       void recvRespRetry() override;
     };
     std::vector<ComputeSidePort> computePorts;
     std::vector<std::deque<PacketPtr>> compute_resp_fifos_;
     
      
     bool response_retryReq[num_ports];  // 记录每个bank是否等待发送请求的重试
     bool response_retryResp[num_ports]; // 记录每个bank是否等待发送响应的重试
     bool recv_req_send_resp[num_ports]; // 记录每个bank是否等待接收响应

     
    
    bool request_retryReq[num_ports];//记录是否重新请求数据
     bool request_retryResp[num_ports]; // 记录每个bank是否等待发送响应的重试
     EventFunctionWrapper tickEvent;
     // 读出轮转偏好：在两个 FULL 缓冲间轮转选择
     bool next_read_idx_[num_ports];
    // --- 行为 ---
    void tick();
    virtual bool recvTimingReq(PacketPtr pkt, uint32_t bank_id) ;
    virtual void sendRespond() = 0;
    void schedule_tick_if_needed();
    void check_current_cmd_completion();
    void check_buf_cmd_completion(int buf_idx,int port_id);
    void maybe_notify_compute_full(int bank_id);
   };
 
 } // namespace GNN
 
 #endif // GNN_DMABUFFER_H_
 