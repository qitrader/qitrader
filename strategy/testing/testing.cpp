#include "testing.h"

#include <fmt/core.h>
#include <glog/logging.h>

namespace strategy::testing {

asio::awaitable<void> Testing::run() {
  LOG(INFO) << "run";
  const auto context = runtime_context();
  if (!context) {
    LOG(WARNING) << "[测试策略] 未注入策略运行时上下文，无法提交订单计划";
    co_return;
  }

  core::domain::OrderPlan plan;
  plan.plan_id = "testing-market-buy";
  plan.idempotency_key.value = plan.plan_id;
  plan.strategy_id = "testing:BTC-USDT-SWAP";
  plan.symbol = "BTC-USDT-SWAP";
  plan.replace_policy = core::domain::ReplacePolicy::KEEP_EXISTING;
  core::domain::OrderIntent intent;
  intent.intent_id = "market-buy";
  intent.symbol = plan.symbol;
  intent.side = core::domain::Side::BUY;
  intent.type = core::domain::OrderType::MARKET;
  intent.quantity = dec_float("0.01");
  plan.intents.push_back(std::move(intent));

  const auto result = context->submit(plan);
  if (!result.accepted) {
    LOG(WARNING) << fmt::format("[测试策略] 市价单未提交: {}", result.error.message);
  } else {
    LOG(INFO) << "[测试策略] 已通过通用运行时提交市价买单计划";
  }
  co_return;
}

void Testing::onMarket(const core::domain::MarketSnapshot& snapshot) {
  LOG(INFO) << fmt::format("onMarket: {} last={}", snapshot.symbol, snapshot.last_price.str());
}

void Testing::onExecution(const core::domain::ExecutionReport& report) {
  LOG(INFO) << fmt::format("onExecution: {} type={} qty={}", report.order_id,
                           static_cast<int>(report.type), report.filled_quantity.str());
}

}  // namespace strategy::testing
