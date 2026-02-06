#include "dram/dram.h"
#include "event/eventq.h"

namespace GNN
{

    Dram::Dram(const std::string &_name)
        : SimObject(_name), port(name() + ".buf_port", this),
          respEvent([this]
                    { this->sendResponse(); }, name() + ".respEvent") {}
    void
    Dram::accessAndRespond(PacketPtr pkt)
    {
        std::cout <<gSim->getCurTick()<< std::endl;
        schedule(respEvent, gSim->getCurTick() + 10); // 10 tick 延迟}
    }
    Dram::DramPort::DramPort(const std::string &_name, Dram *d)
        : ResponsePort(_name), dram(d) {}

    bool Dram::recvTimingReq(PacketPtr pkt)
    {

        //  std::cout << "recvTimingReq\n"<< std::endl;
        pendingPkt = pkt;
        accessAndRespond(pendingPkt);
        return true;
    }

    void Dram::sendResponse()
    {
        bool success = port.sendTimingResp(pendingPkt);
        if (success)
            std::cout << "success" << std::endl;
    }

    Port &
    Dram::getPort(const std::string &if_name, int idx)
    {
        if (if_name != "mem_side")
        {
            return SimObject::getPort(if_name, idx);
        }
        else
        {
            return port;
        }
    }

    bool
Dram::DramPort::recvTimingReq(PacketPtr pkt)
{
    // pass it to the memory controller
    return dram->recvTimingReq(pkt);
}
void
Dram::DramPort::recvRespRetry()
{

}
} // namespace GNN