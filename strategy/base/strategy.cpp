#include "strategy.h"

#include <utility>

#include "core/runtime/strategy_runtime.h"

namespace strategy::base {

void Strategy::set_runtime_context(
    std::shared_ptr<core::runtime::StrategyContext> context,
    std::shared_ptr<core::runtime::StrategyRuntime> runtime) {
  m_runtime_context = std::move(context);
  m_runtime = std::move(runtime);
  if (!m_runtime) return;
  auto weak = std::weak_ptr<Strategy>(shared_from_this());
  m_runtime->setMarketHandler([weak](const core::domain::MarketSnapshot& snapshot) {
    if (auto self = weak.lock()) self->onMarket(snapshot);
  });
  m_runtime->setExecutionHandler([weak](const core::domain::ExecutionReport& report) {
    if (auto self = weak.lock()) self->onExecution(report);
  });
}

asio::awaitable<void> Strategy::init() { co_return; }

}  // namespace strategy::base
