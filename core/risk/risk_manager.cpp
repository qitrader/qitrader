#include "risk_manager.h"

#include <utility>

namespace core::risk {

RiskManager::RiskManager(RiskLimits limits) : m_limits(std::move(limits)) {}

RiskDecision RiskManager::check(const domain::OrderPlan& plan,
                                const domain::PortfolioSnapshot& portfolio,
                                const domain::MarketSnapshot* market) const {
  if (plan.plan_id.empty() || plan.strategy_id.empty() || plan.symbol.empty()) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "plan identity is incomplete"}, {}};
  }
  domain::OrderPlan accepted = plan;
  for (const auto& intent : plan.intents) {
    if (intent.symbol != plan.symbol || intent.quantity <= 0) {
      return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid order intent"}, {}};
    }
    if (intent.type == domain::OrderType::LIMIT && intent.price <= 0) {
      return {false, {domain::ErrorCode::INVALID_ARGUMENT, "limit order price is invalid"}, {}};
    }
    if (m_limits.max_order_quantity > 0 && intent.quantity > m_limits.max_order_quantity) {
      return {false, {domain::ErrorCode::RISK_REJECTED, "order quantity exceeds limit"}, {}};
    }
    if (m_limits.max_order_notional > 0 && intent.type == domain::OrderType::LIMIT &&
        intent.price * intent.quantity > m_limits.max_order_notional) {
      return {false, {domain::ErrorCode::RISK_REJECTED, "order notional exceeds limit"}, {}};
    }
  }

  dec_float current_quantity = 0;
  for (const auto& position : portfolio.positions) {
    if (position.symbol != plan.symbol) continue;
    current_quantity += position.side == domain::Side::BUY
        ? position.quantity : -position.quantity;
  }
  dec_float projected_quantity = current_quantity;
  for (const auto& intent : plan.intents) {
    projected_quantity += intent.side == domain::Side::BUY
        ? intent.quantity : -intent.quantity;
  }
  if (m_limits.max_position_quantity > 0 &&
      abs(projected_quantity) > m_limits.max_position_quantity) {
    return {false, {domain::ErrorCode::RISK_REJECTED, "projected position exceeds limit"}, {}};
  }

  if (m_limits.max_net_exposure > 0) {
    dec_float price = 0;
    if (market) price = market->last_price;
    if (price <= 0 && !plan.intents.empty()) price = plan.intents.front().price;
    if (price > 0 && abs(projected_quantity * price) > m_limits.max_net_exposure) {
      return {false, {domain::ErrorCode::RISK_REJECTED, "net exposure exceeds limit"}, {}};
    }
  }
  return {true, {}, std::move(accepted)};
}

}  // namespace core::risk
