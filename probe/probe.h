

#ifndef __SIM_PROBE_PROBE_HH__
#define __SIM_PROBE_PROBE_HH__

#include <string>
#include <vector>
#include <memory>
#include <functional>
// #include "base/compiler.h"
#include "named.h"
// #include "base/trace.hh"
#include <algorithm>

namespace GNN
{
/** Forward declare the ProbeManager. */
class ProbeManager;
class ProbeListener;

/**
 * Name space containing shared probe point declarations.
 *
 * Probe types that are shared between multiple types of SimObjects
 * should live in this name space. This makes it possible to use a
 * common instrumentation interface for devices such as PMUs that have
 * different implementations in different ISAs.
 */
namespace probing
{
/* Note: This is only here for documentation purposes, new probe
 * points should normally be declared in their own header files. See
 * for example pmu.hh.
 */
}

/**
 * ProbeListener base class; here to simplify things like containers
 * containing multiple types of ProbeListener.
 *
 * Note a ProbeListener is added to the ProbePoint in constructor by
 * using the ProbeManager passed in.
 */
class ProbeListener
{
  public:
    // 构造函数，传入监听器名称
    ProbeListener(std::string _name) : name(std::move(_name)) {}

    virtual ~ProbeListener() = default;
    ProbeListener(const ProbeListener& other) = delete;
    ProbeListener& operator=(const ProbeListener& other) = delete;
    ProbeListener(ProbeListener&& other) noexcept = delete;
    ProbeListener &operator=(ProbeListener &&other) noexcept = delete;
    // 获取监听器名称
    const std::string &getName() const { return name; }

  protected:
    const std::string name; // 监听器名称
};

/**
 * ProbePoint base class; again used to simplify use of ProbePoints
 * in containers and used as to define interface for adding removing
 * listeners to the ProbePoint.
 */
class ProbePoint
{
  protected:
    const std::string name; // 探针点名称
  public:
    // 构造函数，传入管理器和名称
    ProbePoint(ProbeManager *manager, const std::string &name);
    virtual ~ProbePoint() {}

    // 添加监听器到探针点
    virtual void addListener(ProbeListener *listener) = 0;
    // 从探针点移除监听器
    virtual void removeListener(ProbeListener *listener) = 0;
    // 获取探针点名称
    const std::string &getName() const { return name; }
};

struct ProbeListenerCleanup
{
    ProbeListenerCleanup() : manager(nullptr) {};
    explicit ProbeListenerCleanup(ProbeManager *m) : manager(m) {};
    void operator()(ProbeListener *listener) const;

  private:
    ProbeManager *manager;
};

/**
 * This typedef should be used instead of a raw std::unique_ptr<> since
 * we have to disconnect the listener from the manager before calling
 * the ProbeListener destructor. If we were to use a raw
 * std::unique_ptr<Listener> and call disconnect() inside the
 * ProbeListener destructor, we would end up accessing a partially
 * destructed object inside disconnect(), which undefined behaviour and
 * therefore is likely to crash or behave incorrectly.
 */
template <typename Listener = ProbeListener>
using ProbeListenerPtr = std::unique_ptr<Listener, ProbeListenerCleanup>;

/**
 * ProbeManager is a conduit class that lives on each SimObject,
 *  and is used to match up probe listeners with probe points.
 */
class ProbeManager : public Named
{
  private:
    /** Vector for name look-up. */
    std::vector<ProbePoint *> points; // 管理的所有探针点

  public:
    // 构造函数，传入对象名
    ProbeManager(const std::string &obj_name) : Named(obj_name) {}
    virtual ~ProbeManager() {}

    /**
     * @brief 向名为 point_name 的探针点添加监听器。
     * @param point_name 探针点名称
     * @param listener 要添加的监听器
     * @return 是否添加成功
     */
    bool addListener(std::string point_name, ProbeListener &listener);

    /**
     * @brief 从名为 point_name 的探针点移除监听器。
     * @param point_name 探针点名称
     * @param listener 要移除的监听器
     * @return 是否移除成功
     */
    bool removeListener(std::string point_name, ProbeListener &listener);

    /**
     * @brief 向管理器添加一个探针点。
     * @param point 要添加的探针点
     */
    void addPoint(ProbePoint &point);
    // 获取指定名称的第一个探针点
    ProbePoint *getFirstProbePoint(std::string point_name) const;

    // 连接并创建带有回调的监听器
    template <typename Listener, typename... Args>
    ProbeListenerPtr<Listener> connect(Args &&...args)
    {
        ProbeListenerPtr<Listener> result(
            new Listener(std::forward<Args>(args)...),
            ProbeListenerCleanup(this));
        addListener(result->getName(), *result);
        return result;
    }
};

inline void
ProbeListenerCleanup::operator()(ProbeListener *listener) const
{
    manager->removeListener(listener->getName(), *listener);
    delete listener;
}

/**
 * ProbeListenerArgBase: 带参数的监听器基类，定义 notify 接口
 * ProbeListenerArg: 模板监听器，notify 时调用对象成员函数
 * ProbePointArg: 带参数的探针点，能通知所有监听器
 * ProbeListenerArgFunc: 支持lambda回调的监听器
 */
// =================== 中文注释开始 ===================
// ProbeListenerArgBase<Arg>: 带参数的监听器基类，所有带参数监听器都继承自它。
// Arg 是监听的数据类型，notify(val) 会被探针点调用。
//
// ProbeListenerArg<T, Arg>: 模板监听器，T为对象类型，Arg为参数类型。
// 构造时传入对象指针和成员函数指针，notify时自动调用对象的成员函数。
//
// ProbePointArg<Arg>: 带参数的探针点，内部维护所有监听器列表。
// notify(arg) 时会遍历所有监听器，调用其 notify(arg)。
//
// ProbeListenerArgFunc<Arg>: 支持lambda/std::function回调的监听器。
// 构造时传入回调函数，notify时自动调用lambda。
// =================== 中文注释结束 ===================
template <class Arg>
class ProbeListenerArgBase : public ProbeListener
{
  public:
    // 构造函数，传入监听器名称
    ProbeListenerArgBase(std::string name) : ProbeListener(std::move(name)) {}
    // 纯虚函数，子类需实现，通知监听器有新数据
    virtual void notify(const Arg &val) = 0;
};

/**
 * ProbeListenerArg generates a listener for the class of Arg and the
 * class type T which is the class containing the function that notify will
 * call.
 *
 * Note that the function is passed as a pointer on construction.
 */
template <class T, class Arg>
class ProbeListenerArg : public ProbeListenerArgBase<Arg>
{
  private:
    T *object; // 监听的对象指针
    void (T::* function)(const Arg &); // 对象成员函数指针

  public:
    /**
     * @param obj 监听的对象指针
     * @param name 探针点名称
     * @param func 对象成员函数指针，notify时会调用
     */
    ProbeListenerArg(T *obj, std::string name, void (T::*func)(const Arg &))
        : ProbeListenerArgBase<Arg>(std::move(name)),
          object(obj),
          function(func)
    {}

    /**
     * @brief 探针点调用notify时，自动转发到对象成员函数
     * @param val 传递的数据
     */
    void notify(const Arg &val) override { (object->*function)(val); }
};

/**
 * ProbePointArg generates a point for the class of Arg. As ProbePointArgs talk
 * directly to ProbeListenerArgs of the same type, we can store the vector of
 * ProbeListeners as their Arg type (and not as base type).
 *
 * Methods are provided to addListener, removeListener and notify.
 */
template <typename Arg>//通知的数据类型不一样
class ProbePointArg : public ProbePoint
{
    /** The attached listeners. */
    std::vector<ProbeListenerArgBase<Arg> *> listeners; // 所有监听器

  public:
    // 构造函数，传入管理器和探针点名称
    ProbePointArg(ProbeManager *manager, std::string name)
        : ProbePoint(manager, name)
    {
    }

    /**
     * 判断是否有监听器
     * @return 是否有监听器
     */
    bool hasListeners() const { return listeners.size() > 0; }

    /**
     * 添加监听器到探针点
     * @param l 要添加的监听器
     */
    void
    addListener(ProbeListener *l_base) override
    {
        auto *l = dynamic_cast<ProbeListenerArgBase<Arg> *>(l_base);
       
        // 检查监听器是否已添加
        if (std::find(listeners.begin(), listeners.end(), l) ==
            listeners.end()) {
            listeners.push_back(l);
        }
    }

    /**
     * 从探针点移除监听器
     * @param l 要移除的监听器
     */
    void
    removeListener(ProbeListener *l_base) override
    {
        auto *l = dynamic_cast<ProbeListenerArgBase<Arg> *>(l_base);
      
        listeners.erase(std::remove(listeners.begin(), listeners.end(), l),
                        listeners.end());
    }

    /**
     * 探针点触发，通知所有监听器
     * @param arg 要传递的数据
     */
    void
    notify(const Arg &arg)
    {
        for (auto *l : listeners) {
            l->notify(arg);
        }
    }
};


/**
 * ProbeListenerArgFunc generates a listener for the class of Arg and
 * a lambda callback function that is called by the notify.
 *
 * Note that the function is passed as lambda function on construction
 * Example:
 * ProbeListenerArgFunc<MyArg> (myobj->getProbeManager(),
 *                "MyProbePointName", [this](const MyArg &arg)
 *                { my_own_func(arg, xyz...); // do something with arg
 *  }));
 */
template <class Arg>
class ProbeListenerArgFunc : public ProbeListenerArgBase<Arg>
{
  typedef std::function<void(const Arg &)> NotifyFunction;
  private:
    NotifyFunction function; // 回调函数

  public:
    /**
     * @param name 探针点名称
     * @param func 回调函数，notify时会调用
     */
    ProbeListenerArgFunc(const std::string &name, const NotifyFunction &func)
        : ProbeListenerArgBase<Arg>(name), function(func)
    {}

    /**
     * 探针点调用notify时，自动调用lambda回调
     * @param val 传递的数据
     */
    void notify(const Arg &val) override { function(val); }
};


}

#endif//__SIM_PROBE_PROBE_HH__
