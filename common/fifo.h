#ifndef GNN_COMMON_FIFO_H_
#define GNN_COMMON_FIFO_H_

#include <deque>
#include <functional>
#include <vector>
#include <cstddef>

namespace GNN {

// 事件驱动的通用 FIFO：
// - 入队成功后触发 on_data_available 回调
// - 出队成功后触发 on_space_available 回调
// - 可启用/关闭流控（容量限制）
// - 提供批量入队接口
// 说明：不做线程安全封装，由调用方保证并发模型

template <typename T>
class EventDrivenFIFO {
private:
  std::deque<T> queue_;
  std::size_t max_size_;
  bool flow_control_enabled_;

  std::function<void()> on_data_available_;
  std::function<void()> on_space_available_;

public:
  explicit EventDrivenFIFO(std::size_t max_size,
                           bool flow_control = true)
      : max_size_(max_size), flow_control_enabled_(flow_control) {}

  // 设置回调
  void setOnDataAvailable(const std::function<void()> &cb) {
    on_data_available_ = cb;
  }
  void setOnSpaceAvailable(const std::function<void()> &cb) {
    on_space_available_ = cb;
  }

  // 基本状态
  bool empty() const { return queue_.empty(); }
  bool full() const { return flow_control_enabled_ && queue_.size() >= max_size_; }
  std::size_t size() const { return queue_.size(); }
  std::size_t capacity() const { return max_size_; }

  // 清空（不触发回调）
  void clear() { queue_.clear(); }

  // 入队（拷贝/移动）
  bool push(const T &item) {
    if (full()) return false;
    queue_.push_back(item);
    if (on_data_available_) on_data_available_();
    return true;
  }
  bool push(T &&item) {
    if (full()) return false;
    queue_.push_back(std::move(item));
    if (on_data_available_) on_data_available_();
    return true;
  }

  // 批量入队
  bool push_batch(const std::vector<T> &items) {
    if (flow_control_enabled_ && queue_.size() + items.size() > max_size_) {
      return false;
    }
    for (const auto &it : items) queue_.push_back(it);
    if (!items.empty() && on_data_available_) on_data_available_();
    return true;
  }

  // 出队到 out（成功返回 true）
  bool pop(T &out) {
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop_front();
    if (on_space_available_) on_space_available_();
    return true;
  }

  // 仅查看队首（不移除）；不存在则返回 false
  bool peek(T &out) const {
    if (queue_.empty()) return false;
    out = queue_.front();
    return true;
  }
};

} // namespace GNN

#endif // GNN_COMMON_FIFO_H_
