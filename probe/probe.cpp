
#include "probe/probe.h"

namespace GNN
{



ProbePoint::ProbePoint(ProbeManager *manager, const std::string& _name)
    : name(_name)
{
    if (manager) {
        manager->addPoint(*this);
    }
}

bool
ProbeManager::addListener(std::string point_name, ProbeListener &listener)
{
   
       
    bool added = false;
    for (auto p = points.begin(); p != points.end(); ++p) {
        if ((*p)->getName() == point_name) {
            (*p)->addListener(&listener);
            added = true;
        }
    }
    if (!added) {
      
    }
    return added;
}

bool
ProbeManager::removeListener(std::string point_name,
                             ProbeListener &listener)
{
   
    bool removed = false;
    for (auto p = points.begin(); p != points.end(); ++p) {
        if ((*p)->getName() == point_name) {
            (*p)->removeListener(&listener);
            removed = true;
        }
    }
    if (!removed) {
     
    }
    return removed;
}

void
ProbeManager::addPoint(ProbePoint &point)
{
  
    if (getFirstProbePoint(point.getName())) {
 
        return;
    }
    points.push_back(&point);
}

ProbePoint *
ProbeManager::getFirstProbePoint(std::string point_name) const
{
    for (auto p : points) {
        if (p->getName() == point_name) {
            return p;
        }
    }
    return nullptr;
}
}


