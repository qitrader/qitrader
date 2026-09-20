#include "strategy_runtime_component.h"

#include <boost/asio/detached.hpp>
#include <glog/logging.h>

#include "object.h"

namespace core::runtime {

asio::awaitable<void> StrategyRuntimeComponent::init() {
  LOG(INFO) << "通用运行时组件初始化完成";
  if (!m_engine || !m_runtime) co_return;
  auto self = shared_from_this();
  m_engine->register_callback<engine::TickData>(
      engine::EventType::kTick,
      [self](engine::TickDataPtr) { return self->dispatchMarket(); });
  m_engine->register_callback<engine::BarData>(
      engine::EventType::kBar,
      [self](engine::BarDataPtr) { return self->dispatchMarket(); });
  m_engine->register_callback<engine::Book>(
      engine::EventType::kBook,
      [self](engine::BookPtr) { return self->dispatchMarket(); });
  co_return;
}

asio::awaitable<void> StrategyRuntimeComponent::dispatchMarket() {
  if (!m_runtime || !m_runtime->context()) co_return;
  const auto* market = m_runtime->context()->market();
  if (market) m_runtime->notifyMarket(*market);
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
  m_runtime->close();
  if (!co_await m_runtime->waitIdleAsync(50)) {
    LOG(WARNING) << "通用运行时命令队列在引擎停止前未完全排空";
  }
  m_runtime->stop();
}

}  // namespace core::runtime
