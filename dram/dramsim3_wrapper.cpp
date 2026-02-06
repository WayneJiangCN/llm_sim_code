/*
 * @Author: wayne 1448119477@qq.com
 * @Date: 2025-08-25 16:26:59
 * @LastEditors: wayne 1448119477@qq.com
 * @LastEditTime: 2026-01-19 16:54:04
 * @FilePath: /sim_v3/src/dram/dramsim3_wrapper.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2025-08-25 16:24:39
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2025-08-25 16:26:59
 * @FilePath: /sim_v3/src/dram/dramsim3_wrapper.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "dramsim3_wrapper.h"
namespace GNN
{
    void dramsim3_wrapper::print_stats()
    {
        memory_system_1->PrintStats();
    } // dramsim3 print_stats
    void dramsim3_wrapper::init()
    {

        schedule(tickEvent, curTick());
    }
    void dramsim3_wrapper::reset_stats()
    {
        memory_system_1->ResetStats();
    } // dramsim3 reset_stats

    bool dramsim3_wrapper::can_accept(uint64_t addr, bool is_write)
    {
        return memory_system_1->WillAcceptTransaction(addr, is_write);
    } // dramsim3 willAcceptTransaction

    void dramsim3_wrapper::send_request(uint64_t addr, bool is_write)
    {

        bool success = memory_system_1->AddTransaction(addr, is_write);
        assert(success);
    } // dramsim3 add read trans

    unsigned int dramsim3_wrapper::get_busrt_length() const
    {
        return memory_system_1->GetBurstLength();
    } // dramsim3 GetBurstLength

    unsigned int dramsim3_wrapper::get_bandwidth() const
    {
        return memory_system_1->GetBusBits();
    } // dramsim3 GetBurstBirs

    double dramsim3_wrapper::get_frequency() const
    {
        return 1 / (memory_system_1->GetTCK());
    } // dramsim3 get 1/TCK

    unsigned int dramsim3_wrapper::get_channel(address_t addr) const
    {
        return memory_system_1->GetChannel(addr);
    }

    void dramsim3_wrapper::tick()
    {

        memory_system_1->ClockTick();
        if(!tickEvent.scheduled())
        schedule(tickEvent, curTick() + 1);
    }
}
