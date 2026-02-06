#ifndef __EVENT_EVENTQ_H__
#define __EVENT_EVENTQ_H__

#include "common/common.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <functional>
#include <iosfwd>
#include <list>
#include <memory>
#include <string>
#include "common/debug.h"
namespace GNN {
class EventQueue;
extern EventQueue *gSim;
extern uint64_t curTick();
class EventBase {
public:
  typedef int8_t Priority;
  static const Priority Minimum_Pri = -102;
  static const Priority Debug_Enable_Pri = -101;
  static const Priority Debug_Break_Pri = -100;
  static const Priority CPU_Switch_Pri = -31;
  static const Priority Delayed_Writeback_Pri = -1;
  static const Priority Default_Pri = 0;
  static const Priority DVFS_Update_Pri = 31;
  static const Priority Serialize_Pri = 32;
  static const Priority CPU_Tick_Pri = 50;
  static const Priority CPU_Exit_Pri = 64;
  static const Priority Stat_Event_Pri = 90;
  static const Priority Progress_Event_Pri = 95;
  static const Priority Sim_Exit_Pri = 100;
  static const Priority Maximum_Pri = 102;
};

class Event : public EventBase {

  friend class EventQueue;

protected:
  Event *nextBin;
  Event *nextInBin;
  static Event *insertBefore(Event *event, Event *curr);
  static Event *removeItem(Event *event, Event *last);
  Tick _when;
  Priority _priority;
  bool _scheduled = false;
  void setWhen(Tick when) { _when = when; }
  bool initialized() const { return true; }

public:
  Event(Priority p = Default_Pri)
      : nextBin(nullptr), nextInBin(nullptr), _when(0), _priority(p) {}
  virtual ~Event();
  virtual const std::string name() const;
  virtual const char *description() const;

  virtual void process() = 0;
  Tick when() const { return _when; }
  Priority priority() const { return _priority; }
  bool scheduled() const { return _scheduled; }
  void release() {
    if (!scheduled())
      delete this;
  }
};

inline bool operator<(const Event &l, const Event &r) {
  return l.when() < r.when() ||
         (l.when() == r.when() && l.priority() < r.priority());
}
inline bool operator>(const Event &l, const Event &r) {
  return l.when() > r.when() ||
         (l.when() == r.when() && l.priority() > r.priority());
}
inline bool operator<=(const Event &l, const Event &r) {
  return l.when() < r.when() ||
         (l.when() == r.when() && l.priority() <= r.priority());
}
inline bool operator>=(const Event &l, const Event &r) {
  return l.when() > r.when() ||
         (l.when() == r.when() && l.priority() >= r.priority());
}
inline bool operator==(const Event &l, const Event &r) {
  return l.when() == r.when() && l.priority() == r.priority();
}
inline bool operator!=(const Event &l, const Event &r) {
  return l.when() != r.when() || l.priority() != r.priority();
}

class EventQueue {
private:
  std::string objName;
  Event *head;
  Tick _curTick;
  void insert(Event *event);
  void remove(Event *event);
  EventQueue(const EventQueue &);

public:
  EventQueue(const std::string &n);
  virtual const std::string name() const { return objName; }
  void name(const std::string &st) { objName = st; }
  void schedule(Event *event, Tick when) {
    if (event->scheduled())
  //  std::cout<<"schedule event: " << event->name() << " " << event->scheduled() << std::endl;
    assert(!event->scheduled());
    assert(when >= getCurTick());
    event->setWhen(when);
    insert(event);

  }
  void deschedule(Event *event) { remove(event); }
  void reschedule(Event *event, Tick when) {
    assert(when >= getCurTick());
    event->setWhen(when);
    insert(event);
  }
  Tick nextTick() const { return head->when(); }
  void setCurTick(Tick newVal) { _curTick = newVal; }
  Tick getCurTick() const { return _curTick; }
  Event *getHead() const { return head; }
  Event *serviceOne();
  void serviceEvents(Tick when) {
    while (!empty()) {
      if (nextTick() > when)
        break;
      serviceOne();
    }
    setCurTick(when);
  }
  bool empty() const { return head == NULL; }

  Event *replaceHead(Event *s);
  virtual ~EventQueue() {
    while (!empty())
      deschedule(getHead());
  }
};

class EventManager {
protected:
  EventQueue *eventq;

public:
  // EventManager(EventManager &em) : eventq(em.eventq) {}
  // EventManager(EventManager *em) : eventq(em->eventq) {}
  EventManager() { eventq = gSim; }
  EventQueue *eventQueue() const { return eventq; }
  void schedule(Event &event, Tick when) {
  //  std::cout<<"schedule event: " << event.name()<<",time:"<<when <<std::endl;
    eventq->schedule(&event, when);
  }

  void deschedule(Event &event) { eventq->deschedule(&event); }
  void reschedule(Event &event, Tick when, bool always = false) {
    eventq->reschedule(&event, when);
  }
  void schedule(Event *event, Tick when) {

    eventq->schedule(event, when);
  }
  void deschedule(Event *event) { eventq->deschedule(event); }
  void reschedule(Event *event, Tick when, bool always = false) {
    eventq->reschedule(event, when);
  }
  void setCurTick(Tick newVal) { eventq->setCurTick(newVal); }
};

// template <auto F>
// class MemberEventWrapper final: public Event, public Named
// {
//     using CLASS = MemberFunctionClass_t<F>;
//     static_assert(std::is_same_v<void, MemberFunctionReturn_t<F>>);
//     static_assert(std::is_same_v<MemberFunctionArgsTuple_t<F>,
//     std::tuple<>>);
// public:
//     [[deprecated("Use reference version of this constructor instead")]]
//     MemberEventWrapper(CLASS *object,
//                        bool del = false,
//                        Priority p = Default_Pri):
//         MemberEventWrapper{*object, del, p}
//     {}
//     MemberEventWrapper(CLASS &object,
//                        bool del = false,
//                        Priority p = Default_Pri):
//         Event(p),
//         Named(object.name() + ".wrapped_event"),
//         mObject(&object)
//     {
//     }
//     void process() override {
//         (mObject->*F)();
//     }
//     const char *description() const override { return "EventWrapped"; }
// private:
//     CLASS *mObject;
// };

// template <class T, void (T::* F)()>
// using EventWrapper [[deprecated("Use MemberEventWrapper instead")]]
//     = MemberEventWrapper<F>;

class EventFunctionWrapper : public Event {
private:
  std::function<void(void)> callback;
  std::string _name;

public:
  EventFunctionWrapper(const std::function<void(void)> &callback,
                       const std::string &name, bool del = false,
                       Priority p = Default_Pri)
      : Event(p), callback(callback), _name(name) {}


  void process() {
    //std::cout<<name()<<std::endl;
    callback(); }
  const std::string name() const { return _name + ".wrapped_function_event"; }
  const char *description() const { return "EventFunctionWrapped"; }
};
inline Tick curTick() {
  if (gSim)
    return gSim->getCurTick();
  return 0;
}
} // namespace GNN

// 优先级
#endif // __SIM_EVENTQ_HH__
