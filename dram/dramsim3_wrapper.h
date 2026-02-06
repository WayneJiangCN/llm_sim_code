#ifndef DRAMSIM3_WRAPPER
#define DRAMSIM3_WRAPPER
#include "buffer/buffer.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "common/common.h"
#include "common/debug.h"
#include "common/define.h"
#include "common/object.h"
#include "common/packet.h"
#include "dram/sim_dram_storage.h"
#include "event/eventq.h"
#include "memory_system.h"
#define QUEUE_SIZE 64
namespace GNN
{
  class Buffer;  // 前向声明
  class dramsim3_wrapper : public SimObject
  {
  private:
    unsigned int depth;
    unsigned int burst_length;
    unsigned int bandwidth;
    double       frequency;

    std::ofstream trace_out_file_;

    bool vld4repeate_ch[CHANNEL_NUM][64];
    bool channle_vld[CHANNEL_NUM] = { false };

    bool is_ch_rd_send[CHANNEL_NUM] = { false };
    bool is_ch_wr_send[CHANNEL_NUM] = { false };

    // 事件驱动集成：记录每个请求地址等待的Buffer
    std::unordered_map<uint64_t, Buffer*> waitingAddrToBuf;

    // 多通道回调
    std::vector<std::function<void(PacketPtr)>> read_callbacks;
    std::vector<std::function<void(PacketPtr)>> write_callbacks;

    // // 模拟DRAM存储：独立类，提供4GB、burst=64支持
    // SimDramStorage sim_storage;

  public:
    int                     cycle_num;
    void                    init() override;
    dramsim3::MemorySystem* memory_system_1;

    // // PacketPtr 直接接口
    // bool writePacket(PacketPtr pkt) { return sim_storage.writePacket(pkt); }
    // bool readPacket(PacketPtr pkt) { return sim_storage.readPacket(pkt); }

    dramsim3_wrapper(const std::string& config_file,
                     const std::string& output_dir,
                     const std::string& trace_out_file)
      : SimObject("dramsim3_wrapper"), tickEvent([this] { tick(); }, name())
    {
      memory_system_1 = (new dramsim3::MemorySystem(
        config_file,
        output_dir,
        std::bind(&dramsim3_wrapper::global_read_callback, this, std::placeholders::_1),
        std::bind(&dramsim3_wrapper::global_write_callback, this, std::placeholders::_1)));
      burst_length    = memory_system_1->GetBurstLength();
      bandwidth       = memory_system_1->GetQueueSize();
      frequency       = 1 / (memory_system_1->GetTCK());
      std::cout << "burst_length:" << burst_length << " bandwidth:" << bandwidth
                << " frequency:" << frequency << std::endl;
      for (int i = 0; i < CHANNEL_NUM; i++)
      {
        for (int j = 0; j < 64; j++)
        {
          vld4repeate_ch[i][j] = false;
        }
      }
      read_callbacks.resize(CHANNEL_NUM);
      write_callbacks.resize(CHANNEL_NUM);
    }
    void global_read_callback(uint64_t addr)
    {
      // 创建读包，wrapper 负责释放
      PacketPtr pkt = PacketManager::create_read_packet(addr, BURST_BITS / STORAGE_SIZE);

      int ch = this->get_channel(addr);
      if (read_callbacks[ch])
      {
        read_callbacks[ch](pkt);
      }
      // 所有权转移到接收回调（DRAMsim3::readComplete），由其在响应发送后释放
    }
    void global_write_callback(uint64_t addr)
    {
      // 创建写包，示例中填充16个word，wrapper 负责释放
      std::vector<storage_t> dummy(64, 0);
      PacketPtr              pkt = PacketManager::create_write_packet(addr, dummy);

      int ch = this->get_channel(addr);
      if (write_callbacks[ch])
      {
        write_callbacks[ch](pkt);
      }
      // 所有权转移到接收回调（DRAMsim3::writeComplete），由其在完成处理后释放
    }
    ~dramsim3_wrapper() { }

    // 注册回调
    void set_read_callback(int channel, std::function<void(PacketPtr)> cb)
    {
      if (channel >= 0 && channel < CHANNEL_NUM)
        read_callbacks[channel] = cb;
    }
    void set_write_callback(int channel, std::function<void(PacketPtr)> cb)
    {
      if (channel >= 0 && channel < CHANNEL_NUM)
        write_callbacks[channel] = cb;
    }

    void request_read(uint64_t addr)
    {

      int ch = this->get_channel(addr);
      std::cout << "[Wrapper]    回调函数被触发！" << std::endl;
      // 1. 查表找到等待该地址的Buffer
      auto it = waitingAddrToBuf.find(addr);
      if (it != waitingAddrToBuf.end() && it->second)
      {
        Buffer* buf = it->second;

        int data = rand() % 100;
        // buf->DramDataReady(addr, data);
        // 3. 清理登记
        waitingAddrToBuf.erase(it);
      }
      else
      {
        // 没有登记，可能是bug或多余回调
        std::cerr << "[dramsim3_wrapper] Warning: No Buffer waiting for addr " << addr << std::endl;
      }

      //  std::cout << "read_call_back:" << "addr:" << addr << ", ch:" << ch << " clk:" << cycle_g << std::endl;
      if (!channle_vld[ch])
      {
        channle_vld[ch] = true;
      }
      else
      {
        bool is_send = false;
        for (int i = 0; i < 128; i++)
        {
          if (!vld4repeate_ch[ch][i])
          {
            vld4repeate_ch[ch][i] = true;
            is_send               = true;
            break;
          }
        }
        if (!is_send)
        {
          throw std::invalid_argument("repeate full");
        }
      }
    }

    EventFunctionWrapper tickEvent;

    void print_stats();
    void reset_stats();
    bool can_accept(uint64_t addr, bool is_write);
    void send_request(uint64_t addr, bool is_write);  // 已被替换

    unsigned int get_busrt_length() const;
    unsigned int get_bandwidth() const;
    double       get_frequency() const;
    unsigned int get_channel(address_t address) const;

    unsigned int validate_dram_reads(address_t* read_address);

    void tick();
  };
}  // namespace GNN
#endif  // DRAMSIM3_WRAPPER_H
