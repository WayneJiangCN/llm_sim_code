#include "event/eventq.h"
#include "eventq.h"
#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace GNN {
EventQueue *gSim;
Event::~Event() {}

const std::string Event::name() const { return "Event"; }

Event *Event::insertBefore(Event *event, Event *curr) {
  if (!curr || *event < *curr) {
    event->nextBin = curr;
    event->nextInBin = NULL;
  } else {
    event->nextBin = curr->nextBin;
    event->nextInBin = curr;
  }
  // 防止 event->nextBin == event
  assert(event != event->nextBin);
  return event;
}

Event *Event::removeItem(Event *event, Event *top) {
  Event *curr = top;
  Event *next = top->nextInBin;
  if (event == top) {
    if (!next)
      return top->nextBin;
    next->nextBin = top->nextBin;
    return next;
  }
  while (event != next) {
    if (!next)
      throw std::runtime_error("event not found!");
    curr = next;
    next = next->nextInBin;
  }
  curr->nextInBin = next->nextInBin;
  return top;
}

void EventQueue::insert(Event *event) {
  event->_scheduled = true;
  if (!head || *event <= *head) {
    head = Event::insertBefore(event, head);
    return;
  }

  Event *prev = head;
  Event *curr = head->nextBin;
  while (curr && *curr < *event) {
    prev = curr;
    curr = curr->nextBin;
  }
  assert(Event::insertBefore(event, curr));
  prev->nextBin = Event::insertBefore(event, curr);

   
}

void EventQueue::remove(Event *event) {
  event->_scheduled = false;
  if (head == NULL)
    throw std::runtime_error("event not found!");
  if (*head == *event) {
    head = Event::removeItem(event, head);
    return;
  }
  Event *prev = head;
  Event *curr = head->nextBin;
  while (curr && *curr < *event) {
    prev = curr;
    curr = curr->nextBin;
  }
  if (!curr || *curr != *event)
    throw std::runtime_error("event not found!");
  prev->nextBin = Event::removeItem(event, curr);
}

Event *EventQueue::serviceOne() {
  Event *event = head;
  Event *next = head->nextInBin;
  event->_scheduled = false;
  if (next) {
    next->nextBin = head->nextBin;
    head = next;
  } else {
    head = head->nextBin;
  }
  setCurTick(event->when());
  // D_DEBUG("EVENTQ","%s,process event time :%d",event->name(), event->when());
  event->process();
   // event->release();

  return nullptr;
}

Event *EventQueue::replaceHead(Event *s) {
  Event *t = head;
  head = s;
  return t;
}

const char *Event::description() const { return "generic"; }

EventQueue::EventQueue(const std::string &n)
    : objName(n), head(NULL), _curTick(0) {}

} // namespace GNN
