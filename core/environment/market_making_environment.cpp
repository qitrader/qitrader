#include "market_making_environment.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace core::environment {

MarketMakingEnvironment::MarketMakingEnvironment(MarketMakingEnvironmentConfig config)
    : m_config(std::move(config)) {
  m_config.levels = std::max(1, m_config.levels);
  if (m_config.order_size <= 0) m_config.order_size = dec_float(1);
  if (m_config.inventory_limit <= 0) m_config.inventory_limit = m_config.order_size;
}

Observation MarketMakingEnvironment::reset() {
  m_terminal = false;
  m_observation = {};
  return m_observation;
}

void MarketMakingEnvironment::setState(Observation observation) {
  m_observation = std::move(observation);
}

domain::OrderPlan MarketMakingEnvironment::buildPlan(const Action& action) const {
  domain::OrderPlan plan;
  plan.plan_id = action.action_id.empty()
      ? std::to_string(m_observation.timestamp_ms) : action.action_id;
  plan.idempotency_key.value = plan.plan_id;
  plan.strategy_id = m_config.strategy_id;
  plan.symbol = m_observation.market.symbol;
  plan.timestamp_ms = m_observation.timestamp_ms;
  plan.replace_policy = domain::ReplacePolicy::CANCEL_MISSING;

  const std::size_t expected = static_cast<std::size_t>(2 * m_config.levels);
  if (action.values.size() != expected || plan.symbol.empty()) return plan;

  dec_float inventory = 0;
  for (const auto& position : m_observation.portfolio.positions) {
    if (position.symbol == plan.symbol) {
      inventory += position.side == domain::Side::BUY
          ? position.quantity : -position.quantity;
    }
  }
  const double normalized_inventory = inventory.convert_to<double>() /
      std::max(1e-12, m_config.inventory_limit.convert_to<double>());
  const double mid = m_observation.market.last_price.convert_to<double>();

  for (int level = 0; level < m_config.levels; ++level) {
    const double bid_share = std::clamp(action.values[static_cast<std::size_t>(level)], 0.0, 1.0);
    const double ask_share = std::clamp(action.values[static_cast<std::size_t>(m_config.levels + level)], 0.0, 1.0);
    if (bid_share > 0 && normalized_inventory < 1.0) {
      domain::OrderIntent intent;
      intent.intent_id = "bid-" + std::to_string(level);
      intent.symbol = plan.symbol;
      intent.side = domain::Side::BUY;
      intent.type = domain::OrderType::LIMIT;
      intent.level = level;
      intent.quantity = m_config.order_size * bid_share;
      intent.price = level < static_cast<int>(m_observation.market.bids.size())
          ? m_observation.market.bids[static_cast<std::size_t>(level)].price
          : dec_float(std::max(0.0, mid - static_cast<double>(level + 1)));
      if (intent.price > 0 && intent.quantity > 0) plan.intents.push_back(intent);
    }
    if (ask_share > 0 && normalized_inventory > -1.0) {
      domain::OrderIntent intent;
      intent.intent_id = "ask-" + std::to_string(level);
      intent.symbol = plan.symbol;
      intent.side = domain::Side::SELL;
      intent.type = domain::OrderType::LIMIT;
      intent.level = level;
      intent.quantity = m_config.order_size * ask_share;
      intent.price = level < static_cast<int>(m_observation.market.asks.size())
          ? m_observation.market.asks[static_cast<std::size_t>(level)].price
          : dec_float(mid + static_cast<double>(level + 1));
      if (intent.price > 0 && intent.quantity > 0) plan.intents.push_back(intent);
    }
  }
  return plan;
}

double MarketMakingEnvironment::calculateInventoryPenalty(
    const domain::PortfolioSnapshot& portfolio) const {
  double inventory = 0;
  for (const auto& position : portfolio.positions) {
    if (position.symbol == m_observation.market.symbol) {
      inventory += position.side == domain::Side::BUY
          ? position.quantity.convert_to<double>() : -position.quantity.convert_to<double>();
    }
  }
  const double normalized = inventory /
      std::max(1e-12, m_config.inventory_limit.convert_to<double>());
  return m_config.inventory_penalty * normalized * normalized;
}

double MarketMakingEnvironment::calculateSlippage(const domain::OrderPlan& plan,
                                                  const Observation& before,
                                                  const Observation& after) const {
  const std::string& symbol = after.market.symbol.empty() ? plan.symbol
                                                          : after.market.symbol;
  if (symbol.empty()) return 0.0;

  // 用净持仓变化推断本步实际成交量。
  double before_quantity = 0.0;
  double after_quantity = 0.0;
  for (const auto& position : before.portfolio.positions) {
    if (position.symbol == symbol) {
      before_quantity += position.side == domain::Side::BUY
                             ? position.quantity.convert_to<double>()
                             : -position.quantity.convert_to<double>();
    }
  }
  for (const auto& position : after.portfolio.positions) {
    if (position.symbol == symbol) {
      after_quantity += position.side == domain::Side::BUY
                            ? position.quantity.convert_to<double>()
                            : -position.quantity.convert_to<double>();
    }
  }
  const double delta_quantity = after_quantity - before_quantity;
  if (std::fabs(delta_quantity) < 1e-12) return 0.0;

  // 推断实际成交均价：现金净流出扣除手续费后除以成交量。
  const double cash_delta = after.portfolio.cash.convert_to<double>() -
                            before.portfolio.cash.convert_to<double>();
  const double fee_delta = after.portfolio.fees.convert_to<double>() -
                           before.portfolio.fees.convert_to<double>();
  const double outflow = -cash_delta - fee_delta;
  const double implied_price = outflow / delta_quantity;
  if (!(implied_price > 0.0)) return 0.0;

  // 期望成交均价取同方向订单意图的成交量加权价。
  const domain::Side fill_side = delta_quantity > 0 ? domain::Side::BUY
                                                    : domain::Side::SELL;
  double weighted_price = 0.0;
  double weighted_quantity = 0.0;
  for (const auto& intent : plan.intents) {
    if (intent.side != fill_side) continue;
    const double quantity = intent.quantity.convert_to<double>();
    weighted_price += intent.price.convert_to<double>() * quantity;
    weighted_quantity += quantity;
  }
  if (weighted_quantity <= 0.0) return 0.0;
  const double expected_price = weighted_price / weighted_quantity;

  // 买入时实际价高于期望价即为不利滑点，卖出时相反。
  const double signed_diff = fill_side == domain::Side::BUY
                                 ? implied_price - expected_price
                                 : expected_price - implied_price;
  return std::max(0.0, signed_diff) * std::fabs(delta_quantity);
}

Transition MarketMakingEnvironment::step(const Action& action) {
  const Observation previous = m_observation;
  const auto plan = buildPlan(action);
  domain::CommandResult result{true, {}, plan.plan_id};
  if (m_plan_callback) result = m_plan_callback(plan);

  const double previous_equity = previous.portfolio.cash.convert_to<double>();
  const double current_equity = m_observation.portfolio.cash.convert_to<double>();
  const double previous_fees = previous.portfolio.fees.convert_to<double>();
  const double current_fees = m_observation.portfolio.fees.convert_to<double>();

  Reward reward;
  reward.pnl = current_equity - previous_equity;
  reward.inventory_penalty = calculateInventoryPenalty(m_observation.portfolio);
  // pnl 已按实际成交价和手续费计算，这两项只是成本分解，不再重复扣减。
  reward.transaction_cost = current_fees - previous_fees;
  reward.slippage = calculateSlippage(plan, previous, m_observation);
  reward.value = reward.pnl - reward.inventory_penalty;
  if (!result.accepted) reward.value -= 1.0;

  Transition transition{previous, action, reward, m_observation, m_terminal,
                        m_observation.timestamp_ms};
  return transition;
}

}  // namespace core::environment
