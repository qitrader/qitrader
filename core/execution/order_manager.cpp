#include "order_manager.h"

#include <sstream>
#include <utility>
#include <algorithm>
#include <unordered_set>

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

/// 价格相对容差。策略按 tick 量化后，同一档位跨帧的差异只来自浮点往返尾数，
/// 1e-9 相对误差足以吸收噪声，又远小于一个 tick，不会把真实改价误判成"未变"。
constexpr double kPriceRelativeTolerance = 1e-9;
/// 极小价格下的绝对容差下限，避免相对容差在 0 附近退化。
constexpr double kPriceAbsoluteFloor = 1e-12;

dec_float magnitude(const dec_float& value) {
  return value < 0 ? dec_float(-value) : value;
}

bool priceNear(const dec_float& lhs, const dec_float& rhs) {
  const dec_float diff = lhs > rhs ? lhs - rhs : rhs - lhs;
  const dec_float lhs_abs = magnitude(lhs);
  const dec_float rhs_abs = magnitude(rhs);
  const dec_float scale = lhs_abs > rhs_abs ? lhs_abs : rhs_abs;
  const dec_float tolerance = scale * dec_float(kPriceRelativeTolerance);
  const dec_float floor_value = dec_float(kPriceAbsoluteFloor);
  return diff <= (tolerance > floor_value ? tolerance : floor_value);
}

/// 档位身份键：优先 intent_id（"bid-0"/"ask-1"），为空时回退方向+档位+类型。
std::string identityKey(const domain::OrderIntent& intent) {
  if (!intent.intent_id.empty()) return "id:" + intent.intent_id;
  std::ostringstream stream;
  stream << "anon:" << intent.symbol << '|' << static_cast<int>(intent.side) << '|'
         << static_cast<int>(intent.type) << '|' << intent.level << '|'
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

bool OrderManager::sameLevel(const domain::OrderIntent& lhs,
                             const domain::OrderIntent& rhs) {
  return lhs.symbol == rhs.symbol && lhs.side == rhs.side && lhs.type == rhs.type &&
      lhs.level == rhs.level && lhs.reduce_only == rhs.reduce_only &&
      lhs.quantity == rhs.quantity && priceNear(lhs.price, rhs.price);
}

domain::OrderPlanDiff OrderManager::reconcile(const domain::OrderPlan& plan) {
  domain::OrderPlanDiff diff{plan.plan_id, plan.strategy_id, plan.symbol, {}};

  std::vector<std::string> scoped_ids;
  for (const auto& [order_id, active] : m_active_orders) {
    if (active.strategy_id == plan.strategy_id && active.intent.symbol == plan.symbol) {
      scoped_ids.push_back(order_id);
    }
  }
  // 排序后建索引：unordered_map 的遍历顺序不确定，"命中哪一单"会随进程抖动。
  std::sort(scoped_ids.begin(), scoped_ids.end());

  // 指纹只由语义内容 + 作用域内活动订单集合构成，不含每次递增的 plan_id /
  // 幂等键——否则 multilevel 这类每帧生成新 plan_id 的策略永远命中不了短路。
  // 带上活动集合是安全所需：某档成交消失后即使计划内容一字未变也必须重挂，
  // 否则该档会永久空缺。
  std::string content;
  for (const auto& intent : plan.intents) content += intentKey(intent) + ';';
  std::string active_key;
  for (const auto& order_id : scoped_ids) active_key += order_id + ';';
  const std::string fingerprint = content + '#' + active_key;
  const std::string scope_key = plan.strategy_id + '|' + plan.symbol;
  // REPLACE_ALL 的语义是"无条件全撤全挂"，不能被指纹短路跳过。
  if (plan.replace_policy != domain::ReplacePolicy::REPLACE_ALL) {
    const auto previous = m_plan_fingerprints.find(scope_key);
    if (previous != m_plan_fingerprints.end() && previous->second == fingerprint) return diff;
  }
  m_plan_fingerprints[scope_key] = fingerprint;

  if (plan.replace_policy == domain::ReplacePolicy::REPLACE_ALL) {
    for (const auto& order_id : scoped_ids) {
      diff.operations.push_back({domain::OrderOperationType::CANCEL, order_id, std::nullopt});
    }
  }

  std::unordered_map<std::string, std::vector<std::string>> index;
  for (const auto& order_id : scoped_ids) {
    const auto it = m_active_orders.find(order_id);
    if (it == m_active_orders.end()) continue;
    index[identityKey(it->second.intent)].push_back(order_id);
  }

  std::unordered_set<std::string> retained;
  for (const auto& intent : plan.intents) {
    bool found = false;
    if (plan.replace_policy != domain::ReplacePolicy::REPLACE_ALL) {
      const auto bucket = index.find(identityKey(intent));
      if (bucket != index.end()) {
        for (const auto& order_id : bucket->second) {
          if (retained.contains(order_id)) continue;
          const auto it = m_active_orders.find(order_id);
          if (it == m_active_orders.end()) continue;
          if (sameLevel(it->second.intent, intent)) {
            retained.insert(order_id);
            found = true;
            break;
          }
        }
      }
    }
    if (!found) {
      diff.operations.push_back({domain::OrderOperationType::SUBMIT, {}, intent});
    }
  }

  if (plan.replace_policy != domain::ReplacePolicy::KEEP_EXISTING &&
      plan.replace_policy != domain::ReplacePolicy::REPLACE_ALL) {
    for (const auto& order_id : scoped_ids) {
      if (!retained.contains(order_id)) {
        diff.operations.push_back({domain::OrderOperationType::CANCEL, order_id, std::nullopt});
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
