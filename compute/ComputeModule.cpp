#include "compute/ComputeModule.h"

namespace GNN
{

    ComputeModule::ComputeModule(const std::string &name, int active_banks)
        : SimObject(name), active_banks_(active_banks),
          responsePort(name + ".comp_resp", *this),
          requestEvent([this]
                       { requestTick(); }, name + ".requestEvent")
    {
        processed_chunks_per_bank_.assign(active_banks_, 0);
        output_per_bank_.assign(active_banks_, 0LL);
        pending_request_.assign(active_banks_, false);
        in_flight_.assign(active_banks_, false);
        requestPorts.reserve(active_banks_);
        for (int i = 0; i < active_banks_; ++i)
        {
            requestPorts.emplace_back(name + ".comp_side" + std::to_string(i), *this, i);
        }
    }

    void ComputeModule::init()
    {
        // 初始化时标记每个bank有一个待请求
        for (int bank = 0; bank < active_banks_; ++bank)
        {
            pending_request_[bank] = true;
        }
        scheduleRequestIfNeeded(1);
    }

    void ComputeModule::setA(const std::vector<int32_t> &a_vec)
    {
        A_ = a_vec;
        N_ = static_cast<int>(A_.size());
        D_INFO("COMPUTE", "Set A vector, N=%d", N_);
    }

    const std::vector<long long> &ComputeModule::getOutputs() const
    {
        return output_per_bank_;
    }

    void ComputeModule::scheduleRequestIfNeeded(uint32_t delay)
    {
        if (!requestEvent.scheduled())
        {
            // D_INFO("Compute", "[Compute]scheduled 发送");
            schedule(requestEvent, curTick() + delay);
        }
    }

    void ComputeModule::requestTick()
    {
        bool need_reschedule = false;
        bool is_req = false;

        for (int bank = 0; bank < active_banks_; ++bank)
        {

            D_DEBUG("Compute", "[Compute]判断是否请求发送，pending_request_:%d,in_flight_:%d,bank:%d",
                    pending_request_[bank] == true, in_flight_[bank] == true, bank);
            if (pending_request_[bank] && !in_flight_[bank])
            {

                PacketPtr req = PacketManager::create_read_packet(0, 0);
                if (requestPorts[bank].sendTimingReq(req))
                {
                    // 请求已发出，等待响应
                    // D_INFO("Compute", "[Compute]请求已发送");
                    in_flight_[bank] = true;
                    pending_request_[bank] = false;
                }
                else
                {
                    // 发送失败，释放并稍后重试
                    D_INFO("Compute", "[Compute]请求被拒绝，稍后重试");
                    PacketManager::free_packet(req);
                    need_reschedule = true;
                }
            }
            // 若仍有待请求或在飞请求，保持调度
            // if (pending_request_[bank] || in_flight_[bank])
            //     is_req = true;
        }
        // if (is_req && !need_reschedule)
        //     scheduleRequestIfNeeded(1);
    }

    // CompRequestPort implementations
    ComputeModule::CompRequestPort::CompRequestPort(const std::string &name, ComputeModule &o, int id)
        : RequestPort(name), owner(o), bank_id(id) {}

    bool ComputeModule::CompRequestPort::recvTimingResp(PacketPtr pkt)
    {
        return owner.recvTimingResp(pkt, bank_id);
    }

    void ComputeModule::CompRequestPort::recvReqRetry()
    {
        // 对端可再次接收请求，标记该bank需要重试
        owner.pending_request_[bank_id] = true;
        //dma准备好数据，应该是下一拍发起请求，还是立即发起请求，还要判断是否准备好
        owner.scheduleRequestIfNeeded(1);
    }

    bool ComputeModule::recvTimingResp(PacketPtr pkt, uint64_t bank_id)
    {

        // 收到一列数据，执行点积
        const auto &data = pkt->getData();
        long long sum = 0;
        int n = N_ > 0 ? N_ : static_cast<int>(data.size());
        D_INFO("Compute", "[recvTimingResp]收到数据包。base_addr:%d   final_addr:%d ,size:%d", pkt->getAddr()+2*data.size(), pkt->getAddr(),data.size());
        n = std::min(n, static_cast<int>(data.size()));
        if (N_ > 0 && n < N_)
        {
            D_WARN("COMPUTE", "Bank %d data size (%d) is smaller than A vector size (%d)!", bank_id, n, N_);
        }
        for (int i = 0; i < n; ++i)
        {
            sum += static_cast<long long>(A_.empty() ? 1 : A_[i]) *
                   static_cast<long long>(data[i]);
        }
        output_per_bank_[bank_id] = sum;
        processed_chunks_per_bank_[bank_id] += 1;
        D_DEBUG("COMPUTE", "FFN col(bank=%d) done via resp: N=%d, sum=%lld, total_chunks=%lld",
               bank_id, n, sum, processed_chunks_per_bank_[bank_id]);

        // 响应到达，清除在飞标记；立刻安排下一次拉取
        in_flight_[bank_id] = false;
        pending_request_[bank_id] = true;
        scheduleRequestIfNeeded(32);

        delete pkt;
        return true;
    }

    Port &ComputeModule::getPort(const std::string &if_name, int idx)
    {
        if (if_name.rfind("comp_side", 0) == 0)
        {
            int bank = std::stoi(if_name.substr(9));
            if (bank >= 0 && bank < active_banks_)
                return requestPorts[bank];
        }
        if (if_name == "comp_resp")
        {
            return responsePort;
        }
        throw std::runtime_error("No such port: " + if_name);
    }

    // CompResponsePort implementations
    bool ComputeModule::CompResponsePort::recvTimingReq(PacketPtr pkt)
    {
        // Build a response containing current outputs per bank (low 32 bits)
        std::vector<storage_t> data;
        data.reserve(owner.output_per_bank_.size());
        for (auto v : owner.output_per_bank_)
        {
            data.push_back(static_cast<storage_t>(v & 0xFFFFu));
        }

        PacketPtr resp = PacketManager::create_write_packet(0, data);
        bool ok = sendTimingResp(resp);
        if (!ok)
        {
            owner.pending_resp_ = resp;
            // Requestor will call recvRespRetry() later
        }
        else
        {
            // ownership passed/handled by receiver; nothing to do here
        }

        // consume the request packet
        delete pkt;
        return true;
    }

    void ComputeModule::CompResponsePort::recvRespRetry()
    {
        if (owner.pending_resp_)
        {
            if (sendTimingResp(owner.pending_resp_))
            {
                owner.pending_resp_ = nullptr;
            }
        }
    }

} // namespace GNN
