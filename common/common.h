/*
 * @Author: wayne 1448119477@qq.com
 * @Date: 2025-09-05 15:26:01
 * @LastEditors: wayne 1448119477@qq.com
 * @LastEditTime: 2025-09-05 15:26:02
 * @FilePath: /sim_v3/src/common/common.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef __COMMON_COMMON_H__
#define __COMMON_COMMON_H__
#include <iostream>
#include <cstdint>
#include <vector>
#include <functional>

namespace GNN
{


#define DRAM_MODE
using cycle_t = uint64_t;
using Tick = uint64_t;
using addr_t = uint32_t;
using data_t = uint8_t;
using ReadCallback = std::function<void(uint64_t)>;
using ComputeCallback = std::function<void(uint64_t)>;
extern cycle_t cycle_g;
#define DRAM_EPSILON 100


}
#endif