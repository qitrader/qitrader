#include "testing.h"

#include <fmt/core.h>
#include <glog/logging.h>

namespace strategy::testing {

asio::awaitable<void> Testing::run() {
  LOG(INFO) << fmt::format("[测试策略] 启动，等待 {} 的首帧行情后再提交订单计划", m_symbol);
  co_return;
}

void Testing::onMarket(const core::domain::MarketSnapshot& snapshot) {
  LOG(INFO) << fmt::format("onMarket: {} last={}", snapshot.symbol, snapshot.last_price.str());
  if (m_submitted) return;
  const auto context = runtime_context();
  if (!context) {
    LOG(WARNING) << "[测试策略] 未注入策略运行时上下文，无法提交订单计划";
    return;
  }
  // 市价单必须有最新价，因此只在行情到达后提交一次，
  // 否则执行端口会以"价格不可用"直接拒单。
  if (snapshot.symbol != m_symbol || snapshot.last_price <= 0) return;

  core::domain::OrderPlan plan;
  plan.plan_id = "testing-market-buy";
  plan.idempotency_key.value = plan.plan_id;
  plan.strategy_id = "testing:" + m_symbol;
  plan.symbol = m_symbol;
  plan.replace_policy = core::domain::ReplacePolicy::KEEP_EXISTING;
  core::domain::OrderIntent intent;
  intent.intent_id = "market-buy";
  intent.symbol = plan.symbol;
  intent.side = core::domain::Side::BUY;
  intent.type = core::domain::OrderType::MARKET;
  intent.quantity = dec_float("0.01");
  plan.intents.push_back(std::move(intent));

  const auto result = context->submit(plan);
  // 只提交一次：示例策略不需要逐帧重试，重复提交只会放大日志噪音。
  m_submitted = true;
  if (!result.accepted) {
    LOG(WARNING) << fmt::format("[测试策略] 市价单未提交: {}", result.error.message);
  } else {
    LOG(INFO) << "[测试策略] 已通过通用运行时提交市价买单计划";
  }
}

void Testing::onExecution(const core::domain::ExecutionReport& report) {
  LOG(INFO) << fmt::format("onExecution: {} type={} qty={}", report.order_id,
                           static_cast<int>(report.type), report.filled_quantity.str());
}

}  // namespace strategy::testing
