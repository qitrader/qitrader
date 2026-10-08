#include "strategy_runtime_component.h"

#include <boost/asio/detached.hpp>
#include <glog/logging.h>

#include "object.h"

namespace core::runtime {

asio::awaitable<void> StrategyRuntimeComponent::init() {
  LOG(INFO) << "通用运行时组件初始化完成";
  auto engine = m_engine.lock();
  if (!engine || !m_runtime) co_return;
  // 回调同样只持有组件的弱引用：回调生命周期由引擎管理，
  // 强持有会让引擎与组件互相持有而永不析构。
  std::weak_ptr<StrategyRuntimeComponent> weak_self = shared_from_this();
  auto wake = [weak_self](auto) -> asio::awaitable<void> {
    if (auto self = weak_self.lock()) co_await self->dispatchMarket();
  };
  engine->register_callback<engine::TickData>(engine::EventType::kTick, wake);
  engine->register_callback<engine::BarData>(engine::EventType::kBar, wake);
  engine->register_callback<engine::Book>(engine::EventType::kBook, wake);
  co_return;
}

asio::awaitable<void> StrategyRuntimeComponent::dispatchMarket() {
  if (!m_runtime || !m_runtime->context()) co_return;
  const auto* market = m_runtime->context()->market();
  if (market) m_runtime->notifyMarket(*market);
  co_return;
}

asio::awaitable<void> StrategyRuntimeComponent::run() {
  if (!m_runtime) co_return;
  auto engine = m_engine.lock();
  if (!engine) co_return;
  auto runtime = m_runtime;
  asio::co_spawn(engine->executor(),
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
