/*
 * @Author: wayne 1448119477@qq.com
 * @Date: 2025-11-12 12:58:08
 * @LastEditors: wayne 1448119477@qq.com
 * @LastEditTime: 2026-01-20 16:19:35
 * @FilePath: /simulator_simple/src/common/define.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef DEFINE_H
#define DEFINE_H

#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
namespace GNN
{
//#define DEBUG
#define ANALYSIS
#define DRAM_MODE
//data store
#define DATA_STORE       1
#define FLOAT_CAL        1
#define SEG_NUM          2
#define MAC_NUM          16
#define WORD_SIZE        16  //每个Burst的单词大小

#define BITMAP_WORD_BITS 32
#define INST_ADDR_STRIDE 512  //每个指令的地址步长
#define CHANNEL_ADDR_DIF 64
#define SRAM_CAPACITY    262144  //256KB SRAM容量，单位字数

#define STORAGE_SIZE     16            //存储单元的大小
#define BURST_BITS       512           //每个Burst的位数
#define BITMAP_LINE_SIZE 32            //每个行的大小
#define BITMAP_READ_BITS STORAGE_SIZE  //每次从bitmap_words读取的位数（固定16bits）
#define BITMAP_READ_MASK                                 \
  ((1ULL << BITMAP_READ_BITS) - 1)  //读取掩码0xFFFF \ \ \
                                    //每个bitmap word的位数
#define BITMAP_WORD_MASK \
  ((BITMAP_WORD_BITS >= 64) ? ~0ULL : ((1ULL << BITMAP_WORD_BITS) - 1))  //bitmap word掩码

#define BURST_NUM                BURST_BITS / WORD_SIZE  //每个指令中一个burst有多少个数
#define BITMAP_SLICE_ROW_NUM_CFG 2048                    //每个切片的行数
#define BITMAP_SLICE_COL_NUM_CFG 1                       //每个切片的列数

#define TOTAL_SLICE_NUM_CFG      3
#define TOTAL_INST_NUM_CFG       3
#define CHANNEL_NUM              8

#define BITMAP_SIZE              BITMAP_SLICE_ROW_NUM_CFG* BITMAP_WORD_BITS / BURST_BITS
#define WT_SIZE                  BITMAP_SLICE_ROW_NUM_CFG * BITMAP_WORD_BITS * WORD_SIZE / BURST_BITS
#define FW_ROW_SIZE              4
#define FW_SIZE                  FW_ROW_SIZE * BITMAP_SLICE_ROW_NUM_CFG * WORD_SIZE / BURST_BITS
#define FEATURE_NUM_PER_ROW \
  BITMAP_SLICE_ROW_NUM_CFG / BURST_NUM* WORD_SIZE / BURST_BITS * 2  //每个行对应的特征数量

//File CFG
#define FILE_TOTAL_ROW_CFG 11008
#define FILE_TOTAL_COL_CFG 4096
#define FILE_SLICE_ROW_CFG BITMAP_SLICE_ROW_NUM_CFG
#define FILE_SLICE_COL_CFG BITMAP_LINE_SIZE

  // types
  typedef uint64_t cycles_t;
  typedef uint32_t address_t;
  typedef uint32_t reg_t;
  typedef uint16_t storage_t;
  typedef uint64_t bitmap_t;
  typedef uint32_t addr_t;

  extern uint64_t dram_burst_num;
  struct SimulatorConfig
  {
    // Transformer配置

    // 稀疏计算配置
    size_t num_pes              = 8;    // PE数量
    size_t ring_buffer_capacity = 64;   // Ring Buffer容量
    size_t hash_cam_size        = 64;   // Hash CAM大小
    size_t vector_size          = 512;  // 向量大小

    // 仿真配置
    uint64_t max_simulation_cycles = 1000000;  // 最大仿真周期
    bool     enable_statistics     = true;     // 启用统计
    bool     enable_debug          = false;    // 启用调试

    // 数据配置
    std::string weight_file;   // 权重文件路径
    std::string feature_file;  // 特征文件路径
    std::string bitmap_file;   // 位图文件路径
  };
}  // namespace GNN
#endif
