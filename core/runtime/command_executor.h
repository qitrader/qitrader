#ifndef QITRADER_CORE_RUNTIME_COMMAND_EXECUTOR_H_
#define QITRADER_CORE_RUNTIME_COMMAND_EXECUTOR_H_

#include <functional>
#include <memory>

#include "command_queue.h"

namespace core::runtime {

using CommandHandler = std::function<asio::awaitable<void>(const RuntimeCommand&)>;

/**
 * @brief 按需排空命令队列并交给订单执行处理器。
 *
 * 每次排空只处理当前队列中的命令，处理完成后立即结束，
 * 不保留常驻等待协程，避免停止阶段阻塞事件循环退出。
 */
class CommandExecutor {
 public:
  CommandExecutor(std::shared_ptr<CommandQueue> queue, CommandHandler handler)
      : m_queue(std::move(queue)), m_handler(std::move(handler)) {}

  /// 排空当前队列中的命令。
  asio::awaitable<void> drain();

 private:
  std::shared_ptr<CommandQueue> m_queue;
  CommandHandler m_handler;
};

}  // namespace core::runtime

#endif  // QITRADER_CORE_RUNTIME_COMMAND_EXECUTOR_H_
