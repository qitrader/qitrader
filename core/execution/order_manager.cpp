#include "order_manager.h"

#include <sstream>
#include <utility>
#include <algorithm>

namespace core::execution {

namespace {
std::string intentKey(const domain::OrderIntent& intent) {
  std::ostringstream stream;
  stream << intent.intent_id << '|' << intent.symbol << '|'
         << static_cast<int>(intent.side) << '|'
         << static_cast<int>(intent.type) << '|'
         << intent.price.str() << '|' << intent.quantity.str() << '|'
         << intent.level << '|' << intent.expire_at_ms << '|'
         << intent.reduce_only;
  return stream.str();
}
}

bool OrderManager::sameIntent(const domain::OrderIntent& lhs,
                              const domain::OrderIntent& rhs) {
  return lhs.symbol == rhs.symbol && lhs.side == rhs.side && lhs.type == rhs.type &&
      lhs.price == rhs.price && lhs.quantity == rhs.quantity && lhs.level == rhs.level &&
      lhs.reduce_only == rhs.reduce_only;
}

domain::OrderPlanDiff OrderManager::reconcile(const domain::OrderPlan& plan) {
  domain::OrderPlanDiff diff{plan.plan_id, plan.strategy_id, plan.symbol, {}};
  std::string fingerprint;
  for (const auto& intent : plan.intents) fingerprint += intentKey(intent) + ';';
  const std::string plan_key = plan.idempotency_key.empty()
      ? plan.plan_id : plan.idempotency_key.value;
  const auto previous = m_plan_fingerprints.find(plan.strategy_id + '|' + plan.symbol);
  if (previous != m_plan_fingerprints.end() && previous->second == plan_key + '|' + fingerprint) {
    return diff;
  }
  m_plan_fingerprints[plan.strategy_id + '|' + plan.symbol] = plan_key + '|' + fingerprint;

  std::vector<std::string> scoped_ids;
  for (const auto& [order_id, active] : m_active_orders) {
    if (active.strategy_id == plan.strategy_id && active.intent.symbol == plan.symbol) {
      scoped_ids.push_back(order_id);
    }
  }

  if (plan.replace_policy == domain::ReplacePolicy::REPLACE_ALL) {
    for (const auto& order_id : scoped_ids) {
      diff.operations.push_back({domain::OrderOperationType::CANCEL, order_id, std::nullopt});
    }
  }

  std::vector<bool> retained(scoped_ids.size(), false);
  for (const auto& intent : plan.intents) {
    bool found = false;
    if (plan.replace_policy != domain::ReplacePolicy::REPLACE_ALL) {
      for (std::size_t i = 0; i < scoped_ids.size(); ++i) {
        const auto it = m_active_orders.find(scoped_ids[i]);
        if (it == m_active_orders.end() || retained[i]) continue;
        if (sameIntent(it->second.intent, intent)) {
          retained[i] = true;
          found = true;
          break;
        }
      }
    }
    if (!found) {
      diff.operations.push_back({domain::OrderOperationType::SUBMIT, {}, intent});
    }
  }

  if (plan.replace_policy != domain::ReplacePolicy::KEEP_EXISTING &&
      plan.replace_policy != domain::ReplacePolicy::REPLACE_ALL) {
    for (std::size_t i = 0; i < scoped_ids.size(); ++i) {
      if (!retained[i]) {
        diff.operations.push_back({domain::OrderOperationType::CANCEL, scoped_ids[i], std::nullopt});
      }
    }
  }

  std::stable_sort(diff.operations.begin(), diff.operations.end(),
                   [](const domain::OrderOperation& lhs,
                      const domain::OrderOperation& rhs) {
    return lhs.type == domain::OrderOperationType::CANCEL &&
           rhs.type != domain::OrderOperationType::CANCEL;
  });
  return diff;
}

domain::CommandResult OrderManager::registerOrder(const std::string& order_id,
                                                   const std::string& strategy_id,
                                                   const domain::OrderIntent& intent) {
  if (order_id.empty() || strategy_id.empty() || intent.symbol.empty()) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "order identity is incomplete"}, {}};
  }
  if (m_active_orders.contains(order_id)) {
    return {false, {domain::ErrorCode::DUPLICATE, "order already exists"}, order_id};
  }
  m_active_orders.emplace(order_id, domain::ActiveOrder{
      order_id, strategy_id, intent, domain::OrderState::SUBMITTING, dec_float(0)});
  return {true, {}, order_id};
}

domain::CommandResult OrderManager::apply(const domain::ExecutionReport& report) {
  if (report.order_id.empty()) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "order id is empty"}, {}};
  }
  if (!report.execution_id.empty() &&
      !m_processed_executions.insert(report.execution_id).second) {
    return {true, {}, report.order_id};
  }
  auto it = m_active_orders.find(report.order_id);
  if (it == m_active_orders.end()) {
    return {false, {domain::ErrorCode::NOT_FOUND, "active order not found"}, report.order_id};
  }
  auto& order = it->second;
  switch (report.type) {
    case domain::ExecutionEventType::ACCEPTED:
      order.state = domain::OrderState::PENDING;
      break;
    case domain::ExecutionEventType::PENDING:
      order.state = domain::OrderState::PENDING;
      break;
    case domain::ExecutionEventType::PARTIAL_FILL:
      order.state = domain::OrderState::PARTIAL_FILLED;
      if (report.filled_quantity > order.filled_quantity) {
        order.filled_quantity = report.filled_quantity;
      } else if (report.filled_quantity == 0 && report.quantity > 0) {
        order.filled_quantity += report.quantity;
      }
      break;
    case domain::ExecutionEventType::FILL:
      order.state = domain::OrderState::FILLED;
      order.filled_quantity = order.intent.quantity;
      m_active_orders.erase(it);
      break;
    case domain::ExecutionEventType::CANCELLED:
      order.state = domain::OrderState::CANCELLED;
      m_active_orders.erase(it);
      break;
    case domain::ExecutionEventType::REJECTED:
      order.state = domain::OrderState::REJECTED;
      m_active_orders.erase(it);
      break;
  }
  return {true, {}, report.order_id};
}

std::vector<domain::ActiveOrder> OrderManager::activeOrders(
    const std::string& strategy_id) const {
  std::vector<domain::ActiveOrder> result;
  for (const auto& [order_id, order] : m_active_orders) {
    if (strategy_id.empty() || order.strategy_id == strategy_id) result.push_back(order);
  }
  return result;
}

std::optional<domain::ActiveOrder> OrderManager::find(const std::string& order_id) const {
  const auto it = m_active_orders.find(order_id);
  if (it == m_active_orders.end()) return std::nullopt;
  return it->second;
}

}  // namespace core::execution
