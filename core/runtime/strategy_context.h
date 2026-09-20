#ifndef QITRADER_CORE_RUNTIME_STRATEGY_CONTEXT_H_
#define QITRADER_CORE_RUNTIME_STRATEGY_CONTEXT_H_

#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "core/domain/types.h"
#include "core/portfolio/portfolio_ledger.h"
#include "core/risk/risk_manager.h"

namespace core::runtime {

using PlanSubmitter = std::function<domain::CommandResult(const domain::OrderPlan&)>;
using AsyncPlanSubmitter =
    std::function<asio::awaitable<domain::CommandResult>(const domain::OrderPlan&)>;
/// 活动订单查询，参数为策略标签，为空表示查询全部。
using ActiveOrderProvider =
    std::function<std::vector<domain::ActiveOrder>(const std::string& strategy_id)>;

/**
 * @brief 策略使用的通用运行时上下文。
 *
 * 上下文只暴露快照、风控和订单计划提交，不暴露具体 Gateway 或 Engine。
 */
class StrategyContext {
 public:
  StrategyContext(std::shared_ptr<portfolio::PortfolioLedger> ledger,
                  std::shared_ptr<risk::RiskManager> risk,
                  PlanSubmitter submitter = {},
                  AsyncPlanSubmitter async_submitter = {},
                  ActiveOrderProvider active_orders = {})
      : m_ledger(std::move(ledger)),
        m_risk(std::move(risk)),
        m_submitter(std::move(submitter)),
        m_async_submitter(std::move(async_submitter)),
        m_active_orders(std::move(active_orders)) {}

  domain::PortfolioSnapshotPtr portfolio() const {
    return m_ledger ? m_ledger->snapshot() : nullptr;
  }

  risk::RiskDecision check(const domain::OrderPlan& plan,
                           const domain::MarketSnapshot* market = nullptr) const {
    if (!m_ledger || !m_risk) {
      return {false, {domain::ErrorCode::INTERNAL_ERROR, "strategy context is incomplete"}, {}};
    }
    return m_risk->check(plan, *m_ledger->snapshot(), market);
  }

  domain::CommandResult submit(const domain::OrderPlan& plan) const {
    if (!m_submitter) {
      return {false, {domain::ErrorCode::UNSUPPORTED, "order plan submitter is not configured"},
              plan.plan_id};
    }
    const auto decision = check(plan, m_market ? &*m_market : nullptr);
    if (!decision.accepted) return {false, decision.error, plan.plan_id};
    m_strategy_id = plan.strategy_id;
    return m_submitter(decision.plan);
  }

  /**
   * @brief 获取当前策略的活动订单。
   *
   * 活动订单由 `OrderManager` 统一维护，策略无需再自行记账，
   * 因此策略本地不应保留订单表。
   */
  std::vector<domain::ActiveOrder> activeOrders() const {
    if (!m_active_orders) return {};
    return m_active_orders(m_strategy_id);
  }

  /**
   * @brief 异步提交订单计划并执行风险检查。
   */
  asio::awaitable<domain::CommandResult> submitAsync(
      const domain::OrderPlan& plan) const {
    if (!m_async_submitter) {
      co_return domain::CommandResult{
          false, {domain::ErrorCode::UNSUPPORTED,
                  "async order plan submitter is not configured"},
          plan.plan_id};
    }
    const auto decision = check(plan, m_market ? &*m_market : nullptr);
    if (!decision.accepted) {
      co_return domain::CommandResult{false, decision.error, plan.plan_id};
    }
    m_strategy_id = plan.strategy_id;
    co_return co_await m_async_submitter(decision.plan);
  }

  /// 更新当前市场快照，由 `MarketDataFeed` 的行情回调驱动，策略不应直接调用。
  void updateMarket(domain::MarketSnapshot market) { m_market = std::move(market); }

  const domain::MarketSnapshot* market() const {
    return m_market ? &*m_market : nullptr;
  }

  void updateState(domain::StrategyStateSnapshot state) {
    ++state.version;
    m_state = std::move(state);
  }

  domain::StrategyStateSnapshotPtr state() const {
    return std::make_shared<const domain::StrategyStateSnapshot>(m_state);
  }

 private:
  std::shared_ptr<portfolio::PortfolioLedger> m_ledger;
  std::shared_ptr<risk::RiskManager> m_risk;
  PlanSubmitter m_submitter;
  AsyncPlanSubmitter m_async_submitter;
  ActiveOrderProvider m_active_orders;
  /// 最近一次提交使用的策略标签，用于过滤本策略的活动订单
  mutable std::string m_strategy_id;
  std::optional<domain::MarketSnapshot> m_market;
  domain::StrategyStateSnapshot m_state;
};

}  // namespace core::runtime

#endif  // QITRADER_CORE_RUNTIME_STRATEGY_CONTEXT_H_
