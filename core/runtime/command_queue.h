#ifndef QITRADER_CORE_RUNTIME_COMMAND_QUEUE_H_
#define QITRADER_CORE_RUNTIME_COMMAND_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

#include "core/domain/types.h"

namespace core::runtime {

struct RuntimeCommand {
  std::string command_id;
  domain::OrderPlanDiff diff;
};

/**
 * @brief 有界运行时命令队列。
 *
 * 队列只负责保存命令；命令消费通过入队时触发的排空回调完成，
 * 因此不需要后台常驻协程或定时器等待，避免停止阶段阻塞进程退出。
 */
class CommandQueue {
 public:
  CommandQueue(asio::any_io_executor executor, std::size_t capacity)
      : m_capacity(capacity) {}

  /**
   * @brief 非阻塞入队；队列满或已关闭时返回 QUEUE_FULL。
   */
  domain::CommandResult push(RuntimeCommand command);

  /// 非阻塞取出一条命令，无命令时返回空命令。
  RuntimeCommand tryPop();

  /// 设置排空调度回调，入队时调用以触发命令消费。
  void setDrainScheduler(std::function<void()> scheduler) {
    m_drain_scheduler = std::move(scheduler);
  }

  /// 请求开始一次排空，保证同一时间只存在一个排空协程。
  bool beginDrain();

  /// 结束当前排空。
  void endDrain();

  /// 关闭队列。
  void close();

  bool isOpen() const { return m_open.load(); }

 private:
  std::deque<RuntimeCommand> m_pending;
  std::mutex m_mutex;
  std::function<void()> m_drain_scheduler;
  std::size_t m_capacity;
  std::atomic<bool> m_draining{false};
  std::atomic<bool> m_open{true};
};

}  // namespace core::runtime

#endif  // QITRADER_CORE_RUNTIME_COMMAND_QUEUE_H_
