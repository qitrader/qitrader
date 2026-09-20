#ifndef QITRADER_CORE_ENVIRONMENT_MARKET_MAKING_ENVIRONMENT_H_
#define QITRADER_CORE_ENVIRONMENT_MARKET_MAKING_ENVIRONMENT_H_

#include <functional>
#include <string>

#include "trading_environment.h"

namespace core::environment {

struct MarketMakingEnvironmentConfig {
  std::string strategy_id{"market-making"};
  int levels{3};
  dec_float order_size{1};
  dec_float inventory_limit{20};
  double inventory_penalty{0.01};
};

using PlanCallback = std::function<domain::CommandResult(const domain::OrderPlan&)>;

/**
 * @brief 将多层级做市动作转换为目标订单计划并计算风险调整奖励。
 */
class MarketMakingEnvironment final : public TradingEnvironment {
 public:
  explicit MarketMakingEnvironment(MarketMakingEnvironmentConfig config = {});

  Observation reset() override;
  Transition step(const Action& action) override;

  /// 设置当前市场和账本观测。
  void setState(Observation observation);

  /// 设置订单计划提交回调。
  void setPlanCallback(PlanCallback callback) { m_plan_callback = std::move(callback); }

  /// 设置终止状态。
  void setTerminal(bool terminal) { m_terminal = terminal; }

 private:
  domain::OrderPlan buildPlan(const Action& action) const;
  double calculateInventoryPenalty(const domain::PortfolioSnapshot& portfolio) const;

  /// 用净持仓变化和现金变化推断实际成交均价，与订单意图价格比较得到滑点成本。
  double calculateSlippage(const domain::OrderPlan& plan, const Observation& before,
                           const Observation& after) const;

  MarketMakingEnvironmentConfig m_config;
  Observation m_observation;
  PlanCallback m_plan_callback;
  bool m_terminal{false};
};

}  // namespace core::environment

#endif  // QITRADER_CORE_ENVIRONMENT_MARKET_MAKING_ENVIRONMENT_H_
