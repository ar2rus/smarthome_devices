#pragma once
#include <stddef.h>
#include <stdint.h>

// Single cooperative owner; no node allocation or queue-length traversal.
template<class T, size_t Capacity> class BoundedQueue {
  struct Entry { T value{}; uint32_t at = 0; uint16_t budget = 2000; } entries[Capacity];
  size_t head = 0, count = 0;
  void (*destroy)(T);
public:
  explicit BoundedQueue(void (*release)(T)) : destroy(release) {}
  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;
  ~BoundedQueue() { clear(); }
  bool add(T value, uint32_t now = 0, uint16_t budget = 2000) {
    if (!value || count == Capacity) return false;
    entries[(head + count) % Capacity] = {value, now, budget};
    ++count;
    return true;
  }
  T front() const { return count ? entries[head].value : T{}; }
  uint32_t age(uint32_t now) const { return count ? now - entries[head].at : 0; }
  uint16_t budget() const { return count ? entries[head].budget : 0; }
  bool remove(T value) {
    if (!count || front() != value) return false;
    T removed = entries[head].value;
    entries[head] = {};
    head = (head + 1) % Capacity;
    --count;
    if (destroy) destroy(removed);
    return true;
  }
  void clear() { while (count) remove(front()); }
  size_t length() const { return count; }
  bool isEmpty() const { return !count; }
  bool full() const { return count == Capacity; }
};
