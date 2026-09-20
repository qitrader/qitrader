#include "command_executor.h"

#include <glog/logging.h>

namespace core::runtime {

asio::awaitable<void> CommandExecutor::drain() {
  if (!m_queue || !m_handler) co_return;
  if (!m_queue->beginDrain()) co_return;

  try {
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
  } catch (...) {
    m_queue->endDrain();
    throw;
  }
  m_queue->endDrain();
}

}  // namespace core::runtime
