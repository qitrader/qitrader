#include <cassert>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

#include "core/environment/market_making_environment.h"
#include "core/execution/order_manager.h"
#include "core/portfolio/portfolio_ledger.h"
#include "core/risk/risk_manager.h"
#include "strategy/multilevel/actor_critic.h"

namespace {

using core::domain::ExecutionEventType;
using core::domain::ExecutionReport;
using core::domain::OrderIntent;
using core::domain::OrderPlan;
using core::domain::OrderType;
using core::domain::Side;

void testPortfolioLedger() {
  core::portfolio::PortfolioLedger ledger(dec_float("1000"));
  OrderIntent buy;
  buy.intent_id = "buy-1";
  buy.symbol = "BTC-USDT";
  buy.side = Side::BUY;
  buy.type = OrderType::LIMIT;
  buy.price = dec_float("10");
  buy.quantity = dec_float("2");

  assert(ledger.reserve(buy, dec_float("2")).accepted);
  auto reserved = ledger.snapshot();
  assert(reserved->cash == dec_float("1000"));
  assert(reserved->frozen_cash == dec_float("20"));

  ExecutionReport fill;
  fill.type = ExecutionEventType::FILL;
  fill.execution_id = "fill-1";
  fill.order_id = "order-1";
  fill.symbol = buy.symbol;
  fill.side = Side::BUY;
  fill.order_type = OrderType::LIMIT;
  fill.price = dec_float("10");
  fill.quantity = dec_float("1");
  fill.filled_quantity = dec_float("1");
  assert(ledger.apply(fill).accepted);
  assert(ledger.release(buy, dec_float("1")).accepted);

  const auto after_fill = ledger.snapshot();
  assert(after_fill->cash == dec_float("990"));
  assert(after_fill->frozen_cash == dec_float("10"));
  assert(after_fill->positions.size() == 1);
  assert(after_fill->positions.front().quantity == dec_float("1"));

  // 相同 execution_id 重复到达时不得重复记账。
  assert(ledger.apply(fill).accepted);
  const auto after_duplicate = ledger.snapshot();
  assert(after_duplicate->cash == dec_float("990"));
  assert(after_duplicate->positions.front().quantity == dec_float("1"));
}

void testRiskAndOrderManager() {
  core::risk::RiskLimits limits;
  limits.max_order_quantity = dec_float("2");
  limits.max_order_notional = dec_float("100");
  limits.max_position_quantity = dec_float("5");
  core::risk::RiskManager risk(limits);
  core::portfolio::PortfolioLedger ledger(dec_float("1000"));

  OrderPlan plan;
  plan.plan_id = "plan-1";
  plan.strategy_id = "strategy-1";
  plan.symbol = "BTC-USDT";
  OrderIntent intent;
  intent.intent_id = "level-1";
  intent.symbol = plan.symbol;
  intent.side = Side::BUY;
  intent.type = OrderType::LIMIT;
  intent.price = dec_float("20");
  intent.quantity = dec_float("2");
  plan.intents.push_back(intent);

  assert(risk.check(plan, *ledger.snapshot()).accepted);
  intent.quantity = dec_float("6");
  plan.intents.front() = intent;
  assert(!risk.check(plan, *ledger.snapshot()).accepted);
  intent.quantity = dec_float("2");
  plan.intents.front() = intent;

  core::execution::OrderManager orders;
  plan.intents.front() = intent;
  auto first_diff = orders.reconcile(plan);
  assert(first_diff.operations.size() == 1);
  assert(first_diff.operations.front().type == core::domain::OrderOperationType::SUBMIT);
  assert(orders.registerOrder("order-1", plan.strategy_id, intent).accepted);

  auto same_diff = orders.reconcile(plan);
  assert(same_diff.operations.empty());

  intent.price = dec_float("19");
  plan.plan_id = "plan-2";
  plan.intents.front() = intent;
  auto changed_diff = orders.reconcile(plan);
  assert(changed_diff.operations.size() == 2);
  assert(changed_diff.operations.front().type == core::domain::OrderOperationType::SUBMIT ||
         changed_diff.operations.back().type == core::domain::OrderOperationType::SUBMIT);
}

void testPartialFillAndCancelPlan() {
  core::execution::OrderManager orders;
  OrderPlan plan;
  plan.plan_id = "plan-partial";
  plan.idempotency_key.value = "key-partial";
  plan.strategy_id = "strategy-1";
  plan.symbol = "BTC-USDT";
  OrderIntent intent;
  intent.intent_id = "level-1";
  intent.symbol = plan.symbol;
  intent.side = Side::BUY;
  intent.type = OrderType::LIMIT;
  intent.price = dec_float("10");
  intent.quantity = dec_float("2");
  plan.intents.push_back(intent);
  assert(orders.registerOrder("order-partial", plan.strategy_id, intent).accepted);

  ExecutionReport partial;
  partial.type = ExecutionEventType::PARTIAL_FILL;
  partial.execution_id = "execution-partial";
  partial.order_id = "order-partial";
  partial.symbol = plan.symbol;
  partial.side = Side::BUY;
  partial.order_type = OrderType::LIMIT;
  partial.price = dec_float("10");
  partial.filled_quantity = dec_float("1");
  assert(orders.apply(partial).accepted);
  assert(orders.find("order-partial")->filled_quantity == dec_float("1"));

  plan.plan_id = "plan-cancel";
  plan.idempotency_key.value = "key-cancel";
  plan.replace_policy = core::domain::ReplacePolicy::REPLACE_ALL;
  auto diff = orders.reconcile(plan);
  assert(diff.operations.size() == 2);
  assert(diff.operations.front().type == core::domain::OrderOperationType::CANCEL);
  assert(diff.operations.back().type == core::domain::OrderOperationType::SUBMIT);
}

void testMarketMakingEnvironment() {
  core::environment::MarketMakingEnvironment environment;
  core::environment::Observation state;
  state.timestamp_ms = 1000;
  state.market.symbol = "BTC-USDT";
  state.market.last_price = dec_float("100");
  state.market.bids.push_back({dec_float("99"), dec_float("10")});
  state.market.asks.push_back({dec_float("101"), dec_float("10")});
  state.portfolio.cash = dec_float("1000");
  environment.setState(state);

  bool callback_called = false;
  environment.setPlanCallback([&callback_called](const OrderPlan& plan) {
    callback_called = true;
    assert(plan.symbol == "BTC-USDT");
    assert(!plan.intents.empty());
    return core::domain::CommandResult{true, {}, plan.plan_id};
  });

  core::environment::Action action;
  action.action_id = "action-1";
  action.values = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  const auto transition = environment.step(action);
  assert(callback_called);
  assert(transition.action.action_id == "action-1");
  assert(std::isfinite(transition.reward.value));
}

void testRewardCostBreakdown() {
  core::environment::MarketMakingEnvironmentConfig config;
  config.levels = 1;
  config.order_size = dec_float("1");
  config.inventory_limit = dec_float("10");
  config.inventory_penalty = 0.0;  // 排除库存惩罚，聚焦成本分解
  core::environment::MarketMakingEnvironment env(config);

  auto observation = env.reset();
  observation.market.symbol = "BTC-USDT";
  observation.market.last_price = dec_float("100");
  observation.timestamp_ms = 1000;
  observation.portfolio.cash = dec_float("10000");
  env.setState(observation);

  // 计划回调模拟按不利价格成交：期望价 99，实际成交价 101，手续费 1。
  OrderPlan captured;
  env.setPlanCallback([&](const OrderPlan& plan) {
    captured = plan;
    auto filled = observation;
    filled.portfolio.cash = dec_float("9898");
    filled.portfolio.fees = dec_float("1");
    core::domain::PositionSnapshot position;
    position.symbol = "BTC-USDT";
    position.side = Side::BUY;
    position.quantity = dec_float("1");
    position.average_price = dec_float("101");
    filled.portfolio.positions.push_back(position);
    env.setState(filled);
    return core::domain::CommandResult{true, {}, plan.plan_id};
  });

  core::environment::Action action;
  action.action_id = "action-1";
  action.values = {1.0, 0.0};  // 只挂买单，不挂卖单
  const auto transition = env.step(action);

  assert(captured.intents.size() == 1);
  assert(captured.intents.front().side == Side::BUY);
  assert(std::fabs(transition.reward.pnl - (-102.0)) < 1e-6);
  assert(std::fabs(transition.reward.transaction_cost - 1.0) < 1e-6);
  assert(std::fabs(transition.reward.slippage - 2.0) < 1e-6);
  // 手续费和滑点是 pnl 的分解项，不从 value 中重复扣除。
  assert(std::fabs(transition.reward.value - (-102.0)) < 1e-6);
}

void testActorCriticLearns() {
  // 合成对照环境：观察固定不变，奖励只取决于所选动作（动作 0 最优）。
  // 这是线性可分任务，线性 Actor-Critic 必须学会偏向动作 0。
  // 若学不会，说明梯度更新、奖励符号或采样链路存在问题——
  // 这能区分"训练管线有 bug"和"真实行情下本就学不到东西"。
  const std::vector<double> observation{1.0, 0.5};
  // exploration 必须大于 0：确定性策略没有随机性，score function 恒为零。
  strategy::multilevel::ActorCritic policy(2, 3, 0.05, 0.1);

  const double before = policy.sample(observation)[0];
  for (int step = 0; step < 3000; ++step) {
    const auto action = policy.sample(observation);
    // 动作 0 为最优：其占比越高，奖励越高。
    const double reward = 2.0 * action[0] - 1.0;
    policy.update(observation, action, reward, observation, false);
  }
  const double after = policy.sample(observation)[0];
  std::printf("    动作0占比: %.6f -> %.6f\n", before, after);
  assert(after > before + 0.05);

  // 序列化往返后参数必须完全一致，且恢复出的策略仍偏好动作 0。
  // 这里比较参数而非采样值：采样带探索噪声，两次调用本就不应逐位相等。
  const std::string dumped = policy.serialize();
  strategy::multilevel::ActorCritic restored(2, 3, 0.05, 0.1);
  assert(restored.deserialize(dumped));
  assert(restored.serialize() == dumped);
  assert(restored.sample(observation)[0] > before + 0.05);

  // 维度不匹配时必须拒绝，而不是加载出错位的权重
  strategy::multilevel::ActorCritic mismatched(3, 3, 0.05, 0.1);
  assert(!mismatched.deserialize(dumped));
}

}  // namespace

int main() {
  struct TestCase {
    const char* name;
    void (*run)();
  };
  const TestCase cases[] = {
      {"PortfolioLedger 现金冻结/成交/盈亏", &testPortfolioLedger},
      {"RiskManager 风控与 OrderManager 计划差异", &testRiskAndOrderManager},
      {"部分成交、撤单与 REPLACE_ALL", &testPartialFillAndCancelPlan},
      {"MarketMakingEnvironment 动作到订单计划", &testMarketMakingEnvironment},
      {"Reward 手续费与滑点成本分解", &testRewardCostBreakdown},
      {"ActorCritic 在合成任务上可学习且可持久化", &testActorCriticLearns},
  };

  for (const auto& test_case : cases) {
    test_case.run();
    std::printf("[ PASS ] %s\n", test_case.name);
  }
  std::printf("全部 %zu 组核心运行时用例通过\n", std::size(cases));
  return 0;
}
