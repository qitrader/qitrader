#include "command_executor.h"

#include <glog/logging.h>

namespace core::runtime {

namespace {
/// 排空标志的 RAII 守卫：协程被取消或处理器挂起异常退出时也要复位，
/// 否则 m_draining 会永久为真，后续命令滞留队列且无人消费。
class DrainGuard {
 public:
  explicit DrainGuard(CommandQueue* queue) : m_queue(queue) {}
  ~DrainGuard() { if (m_queue) m_queue->endDrain(); }
  DrainGuard(const DrainGuard&) = delete;
  DrainGuard& operator=(const DrainGuard&) = delete;
 private:
  CommandQueue* m_queue;
};
}  // namespace

asio::awaitable<void> CommandExecutor::drain() {
  if (!m_queue || !m_handler) co_return;
  if (!m_queue->beginDrain()) co_return;
  DrainGuard guard(m_queue.get());

  for (;;) {
    if (!m_queue->isOpen()) break;
    auto command = m_queue->tryPop();
    if (command.command_id.empty()) break;
    try {
      co_await m_handler(command);
    } catch (const std::exception& error) {
      LOG(ERROR) << "运行时命令执行失败: " << error.what();
    } catch (...) {
      LOG(ERROR) << "运行时命令执行失败: 未知异常";
    }
  }
  co_return;
}

}  // namespace core::runtime
