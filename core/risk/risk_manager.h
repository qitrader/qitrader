#ifndef QITRADER_CORE_RISK_RISK_MANAGER_H_
#define QITRADER_CORE_RISK_RISK_MANAGER_H_

#include "core/domain/types.h"

namespace core::risk {

struct RiskLimits {
  dec_float max_order_quantity{0};
  dec_float max_order_notional{0};
  dec_float max_position_quantity{0};
  dec_float max_net_exposure{0};
};

struct RiskDecision {
  bool accepted{false};
  domain::Error error;
  domain::OrderPlan plan;
};

/**
 * @brief 在订单计划进入执行层前执行通用风险检查。
 */
class RiskManager {
 public:
  explicit RiskManager(RiskLimits limits = {});

  /// 校验订单计划；零值限制表示不启用对应限制。
  RiskDecision check(const domain::OrderPlan& plan,
                     const domain::PortfolioSnapshot& portfolio,
                     const domain::MarketSnapshot* market = nullptr) const;

  const RiskLimits& limits() const { return m_limits; }
  void setLimits(RiskLimits limits) { m_limits = limits; }

 private:
  RiskLimits m_limits;
};

}  // namespace core::risk

#endif  // QITRADER_CORE_RISK_RISK_MANAGER_H_
