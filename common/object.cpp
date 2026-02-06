#include "object.h"
#include <cassert>

namespace GNN
{
// 静态成员：所有SimObject实例列表
SimObject::SimObjectList SimObject::simObjectList;
SimObjectResolver *SimObject::_objNameResolver = NULL;

// 构造函数：加入全局对象列表，初始化探针管理器
SimObject::SimObject(const std::string &_name) :Named(_name+".Event"){
    simObjectList.push_back(this);
    probeManager =  new ProbeManager(name()); // 如需探针可在子类构造时new ProbeManager(name());
}

SimObject::~SimObject() {
    delete probeManager;
}

// 初始化（所有对象创建后调用）
void SimObject::init() {

}

// 注册探针点（子类可重载）
void SimObject::regProbePoints() {}

// 注册探针监听器（子类可重载）
void SimObject::regProbeListeners() {}

// 获取探针管理器
ProbeManager *SimObject::getProbeManager() {
    return probeManager;
}

// 获取端口（用于模块间连接，子类必须重载实现）
// 参数：if_name 端口名，idx 端口索引（如有多个端口）
// 返回：对应的 Port 引用
Port &SimObject::getPort(const std::string &if_name, int idx) {
    // 这里直接抛出异常或断言，提示必须由子类实现
    std::cerr << "[SimObject] getPort: " <<name()<< " 没有端口名: " << if_name << std::endl;
    assert(false && "No such port in this SimObject (子类必须重载 getPort)");
    // 返回一个静态 dummy_port，防止编译器警告
    static Port dummy_port("dummy");
    return dummy_port;
}

// 启动（仿真前最后初始化，子类可重载）
void SimObject::startup() {}


// 静态：通过名字查找SimObject
SimObject *SimObject::find(const char *name) {
    for (auto *obj : simObjectList) {
        // 假设有name()方法
        if (obj->name() == name)
            return obj;
    }
    return nullptr;
}

// 设置名字解析器
void SimObject::setSimObjectResolver(SimObjectResolver *resolver) {
    _objNameResolver = resolver;
}

// 获取名字解析器
SimObjectResolver *SimObject::getSimObjectResolver() {
    return _objNameResolver;
}


} // namespace gem5
