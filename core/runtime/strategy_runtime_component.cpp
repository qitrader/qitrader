#include "strategy_runtime_component.h"

#include <boost/asio/detached.hpp>
#include <glog/logging.h>

namespace core::runtime {

asio::awaitable<void> StrategyRuntimeComponent::init() {
  LOG(INFO) << "通用运行时组件初始化完成";
  if (m_runtime) LOG(INFO) << "通用运行时组件初始化完成";
  co_return;
}

asio::awaitable<void> StrategyRuntimeComponent::run() {
  if (!m_runtime || !m_engine) co_return;

  auto runtime = m_runtime;
  asio::co_spawn(m_engine->executor(),
      [runtime]() -> asio::awaitable<void> { co_await runtime->run(); },
      asio::detached);
  co_return;
}

asio::awaitable<void> StrategyRuntimeComponent::shutdown() {
  if (!m_runtime) co_return;
  // 先关闭计划入口，避免排空期间继续产生新命令。
  m_runtime->close();
  // 使用较短超时：停止阶段无法保证事件循环继续推进，不能依赖长等待。
  if (!co_await m_runtime->waitIdleAsync(50)) {
    LOG(WARNING) << "通用运行时命令队列在引擎停止前未完全排空";
  }
  m_runtime->stop();
}

}  // namespace core::runtime
