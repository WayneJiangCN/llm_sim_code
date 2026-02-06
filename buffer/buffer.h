/*
 * @Author: wayne 1448119477@qq.com
 * @Date: 2025-11-14 21:21:11
 * @LastEditors: wayne 1448119477@qq.com
 * @LastEditTime: 2025-11-14 21:22:18
 * @FilePath: /simulator_simple/src/buffer/buffer.h
 * @Description: 简化的写Buffer，只向DRAM arbiter写数据
 */
#pragma once
#include <queue>
#include <string>
#include <vector>
#include <stdexcept>

#include "event/eventq.h"
#include "common/common.h"
#include "common/port.h"
#include "common/object.h"
#include "common/packet.h"
#include "common/debug.h"

namespace GNN
{

    class Buffer : public SimObject
    {
    public:
        static constexpr int kDefaultChannels = 8;

        Buffer(const std::string &_name, int num_channels = kDefaultChannels, size_t capacity_per_channel = 8);
        ~Buffer();

        void init() override;

        // 端口获取：名称形如 "<name>.buf_side<id>"
        Port &getPort(const std::string &if_name, int idx = -1) override;

        // 添加写数据到指定通道
        bool enqueueWrite(int channel, addr_t addr, const std::vector<storage_t> &data);
        bool enqueueWrite(int channel, PacketPtr pkt);

        bool isFull(int channel) const;
        bool isEmpty(int channel) const;

        class MemSidePort : public RequestPort
        {
            Buffer &buffer;
            int channel_id;

        public:
            MemSidePort(const std::string &_name, Buffer &buf, int channel);
            bool recvTimingResp(PacketPtr pkt) override;
            void recvReqRetry() override;
        };

    private:
        Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;

        bool recvTimingResp(int channel, PacketPtr pkt);
        void handleRetry(int channel);
        void trySendWrite(int channel);
        void drainWrites();

        int num_channels_;
        size_t maxSizePerChannel_;
        std::vector<MemSidePort> memPorts;
        std::vector<std::queue<PacketPtr>> writeQueues;
        std::vector<size_t> outstandingWrites;
        std::vector<bool> waitingRetry;

        EventFunctionWrapper drainEvent;
    };

} // namespace GNN