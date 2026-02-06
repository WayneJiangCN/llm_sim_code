#include "buffer/buffer.h"
#include "common/debug.h"
#include "common/define.h"
#include "common/object.h"
#include "compute/ComputeModule.h"
#include "dram/dram_arb.h"
#include "dram/dramsim3.h"
#include "dram/dramsim3_wrapper.h"
#include "dram/sim_dram_storage.h"
#include "event/eventq.h"
#include "spare/BitmapBank.h"
#include "spare/DecoderModule.h"
#include "spare/FeatureBank.h"
#include "spare/WeightBank.h"
#include <cstdint>
#include <iostream>
#include <map>

using namespace GNN;

// 双向端口绑定辅助函数
static inline void bindPorts(Port& a, Port& b)
{
  a.bind(b);
  b.bind(a);
}

// 遍历所有对象执行成员函数
static void forEachObject(void (SimObject::*mem_func)())
{
  for (auto* obj : SimObject::simObjectList)
  {
    (obj->*mem_func)();
  }
}
namespace GNN
{
  uint64_t storage_addr_max = 0;
  uint64_t storage_number   = 0;
  uint64_t dram_burst_num   = 0;

}  // namespace GNN
int main()
{
  // 配置常量
  constexpr int         num_banks      = 8;
  constexpr int         dram_buf_size  = 256 * 8;
  constexpr int         num_upstreams  = 4;
  constexpr uint64_t    max_cycles     = 30000000;
  constexpr int         bitmap_size    = BITMAP_SIZE;
  constexpr int         wt_bank_size   = WT_SIZE;
  constexpr int         fw_bank_size   = FW_SIZE;
  // 文件路径
  constexpr const char* config_file    = "./DRAMsim3-master/configs/HBM2_4Gb_x128.ini";
  constexpr const char* output_dir     = ".";
  constexpr const char* trace_out_file = "./output/trace_out_file.txt";

  constexpr const char* layer0_path = "./data/test/llama50";
  int                   num         = BITMAP_SLICE_ROW_NUM_CFG * BURST_BITS / WORD_SIZE * SPARSITY;
  // 初始化仿真系统
  gSim                              = new EventQueue("main_queue");
  miniDebugLevel                    = GNN::DBG_DEBUG;  // SIM_DRAM_STORAGE FILE_READ
  // miniDebugModules = {"SPARSE", "DECODER", "", "",
  //                     "WeightBank", "FeatureBank", "BitmapBank",
  //                     "DmaBuffer","DMA","DRAM_ARB"};
  //    miniDebugModules = {"CAM", "", "DECODER", "BUG", "FILE_READ", "SIM_DRAM_STORAGE", "","CAM"};
  miniDebugModules                  = { "BLOCK", "RESULT", "SIM_DRAM_STORAGE" };
  // 创建存储和数据接口
  SimDramStorage* sim_storages      = new SimDramStorage(0, "*", ".txt");

  // 读取layer_0文件夹下所有子文件夹的数据
  uint64_t layer0_burst_num = sim_storages->readLayer0AllFoldersData(layer0_path);

  // 也可以继续使用原来的方法读取单个文件（如果需要）
  // sim_storages->readDataFile();

  // 创建DRAM控制器和仲裁器
  auto*   wrapper = new dramsim3_wrapper(config_file, output_dir, trace_out_file);
  DramArb dramArb("dram_arb", 128, num_upstreams);

  // 创建Bank模块
  BitmapBank  bitmap_bank("bmap_", 0, bitmap_size, num_banks, layer0_burst_num / bitmap_size / 8);
  WeightBank  weight_bank("w_", 0x1000000, wt_bank_size, num_banks);
  FeatureBank feature_bank("f_", 0x15000000, fw_bank_size, num_banks);

  // 创建解码、计算与写 Buffer 模块
  Buffer        decoder_buffer("decoder_buf", num_banks, BITMAP_LINE_SIZE * FW_ROW_SIZE);
  DecoderModule decoder("decoder_", num_banks, sim_storages, &decoder_buffer);
  ComputeModule compute("compute0", num_banks);

  // 创建DRAM实例
  std::vector<DRAMsim3*> drams;
  drams.reserve(num_banks);
  for (int bank = 0; bank < num_banks; ++bank)
  {
    drams.push_back(new DRAMsim3("dramsim3_" + std::to_string(bank), bank, wrapper));
  }

  // 连接所有端口
  for (int bank = 0; bank < num_banks; ++bank)
  {
    const std::string b = std::to_string(bank);

    // Bank到DMA仲裁器
    bindPorts(bitmap_bank.getPort("bmap_dma_side" + b, bank),
              dramArb.getPort("response" + b + "_0"));
    bindPorts(weight_bank.getPort("w_dma_side" + b, bank), dramArb.getPort("response" + b + "_1"));
    bindPorts(feature_bank.getPort("f_dma_side" + b, bank), dramArb.getPort("response" + b + "_2"));

    // Decoder到Bank
    bindPorts(decoder.getPort("decoder_bmap_side" + b, bank),
              bitmap_bank.getPort("bmap_comp_side" + b, bank));
    bindPorts(decoder.getPort("decoder_w_side" + b, bank),
              weight_bank.getPort("w_comp_side" + b, bank));
    bindPorts(decoder.getPort("decoder_f_side" + b, bank),
              feature_bank.getPort("f_comp_side" + b, bank));

    // Compute到Decoder
    bindPorts(compute.getPort("comp_side" + b), decoder.getPort("decoder_compute_side" + b, bank));

    // Decoder写Buffer到DRAM仲裁器
    bindPorts(decoder_buffer.getPort("buf_side" + b), dramArb.getPort("response" + b + "_3"));

    // DMA仲裁器到DRAM
    bindPorts(dramArb.getPort("request" + b), drams[bank]->getPort("mem_side"));
  }

  // 初始化所有对象
  forEachObject(&SimObject::init);

  // 运行仿真
  std::cout << "\n---- Simulation Start ----" << std::endl;
  while (!gSim->empty() && gSim->getCurTick() < max_cycles)
  {
    gSim->serviceOne();
  }
  std::cout << "---- Simulation End ----" << std::endl;

  delete gSim;
  return 0;
}
