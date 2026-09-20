#ifndef QITRADER_CORE_EXECUTION_ORDER_MANAGER_H_
#define QITRADER_CORE_EXECUTION_ORDER_MANAGER_H_

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/domain/types.h"

namespace core::execution {

/**
 * @brief 将策略目标订单计划收敛为撤单、新单和换单操作。
 *
 * 第一阶段只负责订单生命周期和计划差异，不直接依赖 Engine 或 Gateway。
 */
class OrderManager {
 public:
  /// 计算策略计划与活动订单之间的差异。
  domain::OrderPlanDiff reconcile(const domain::OrderPlan& plan);

  /// 应用执行回报并维护活动订单。
  domain::CommandResult apply(const domain::ExecutionReport& report);

  /// 注册已提交的订单，通常由执行适配器在发送成功后调用。
  domain::CommandResult registerOrder(const std::string& order_id,
                                      const std::string& strategy_id,
                                      const domain::OrderIntent& intent);

  /// 获取活动订单的只读副本。
  std::vector<domain::ActiveOrder> activeOrders(
      const std::string& strategy_id = {}) const;

  /// 查找指定订单。
  std::optional<domain::ActiveOrder> find(const std::string& order_id) const;

 private:
  static bool sameIntent(const domain::OrderIntent& lhs,
                         const domain::OrderIntent& rhs);

  std::unordered_map<std::string, domain::ActiveOrder> m_active_orders;
  std::unordered_map<std::string, std::string> m_plan_fingerprints;
  std::unordered_set<std::string> m_processed_executions;
};

}  // namespace core::execution

#endif  // QITRADER_CORE_EXECUTION_ORDER_MANAGER_H_
