#ifndef __OBJECT__
#define __OBJECT__

#include <string>
#include <vector>
#include "eventq.h"
#include "port.h"
#include "probe/named.h"
#include "probe/probe.h"
namespace GNN
{

class EventManager;
class ProbeManager;
class SimObjectResolver;

// 仿真对象基类，所有模块继承自它
class SimObject : public EventManager, public Named
{
  private:

    static SimObjectResolver *_objNameResolver; // 名字解析器
    ProbeManager *probeManager; // 探针管理器

  public:
    SimObject(const std::string &_name);
    virtual ~SimObject();
    typedef std::vector<SimObject *> SimObjectList;
    static SimObjectList simObjectList; // 所有SimObject实例列表
    // 初始化（所有对象创建后调用）
    virtual void init();
    // 注册探针点
    virtual void regProbePoints();
    // 注册探针监听器
    virtual void regProbeListeners();
    // 获取探针管理器
    ProbeManager *getProbeManager();
    // 获取端口（用于模块间连接）
    virtual Port &getPort(const std::string &if_name, int idx=-1);
    // 启动（仿真前最后初始化）
    virtual void startup();


    // 静态：通过名字查找SimObject
    static SimObject *find(const char *name);
    // 设置/获取名字解析器
    static void setSimObjectResolver(SimObjectResolver *resolver);
    static SimObjectResolver *getSimObjectResolver();
};

#define PARAMS(type)

class SimObjectResolver
{
  public:
    virtual ~SimObjectResolver() { }
    virtual SimObject *resolveSimObject(const std::string &name) = 0;
};


} // namespace gem5

#endif // __SIM_OBJECT_HH__
