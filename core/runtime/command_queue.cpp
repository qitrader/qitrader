#include "command_queue.h"

#include <utility>

namespace core::runtime {

domain::CommandResult CommandQueue::push(RuntimeCommand command) {
  const std::string command_id = command.command_id;
  if (!m_open.load()) {
    return {false, {domain::ErrorCode::QUEUE_FULL, "command queue is closed"}, command_id};
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending.size() >= m_capacity) {
      return {false, {domain::ErrorCode::QUEUE_FULL, "command queue is full"}, command_id};
    }
    m_pending.push_back(std::move(command));
  }
  if (m_drain_scheduler) m_drain_scheduler();
  return {true, {}, command_id};
}

RuntimeCommand CommandQueue::tryPop() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_pending.empty()) return {};
  auto command = std::move(m_pending.front());
  m_pending.pop_front();
  return command;
}

bool CommandQueue::beginDrain() {
  bool expected = false;
  return m_draining.compare_exchange_strong(expected, true);
}

void CommandQueue::endDrain() {
  m_draining.store(false);
}

void CommandQueue::close() {
  m_open.store(false);
}

}  // namespace core::runtime
