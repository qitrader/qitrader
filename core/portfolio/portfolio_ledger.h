#ifndef QITRADER_CORE_PORTFOLIO_PORTFOLIO_LEDGER_H_
#define QITRADER_CORE_PORTFOLIO_PORTFOLIO_LEDGER_H_

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/domain/types.h"

namespace core::portfolio {

/**
 * @brief 统一维护资金、持仓和执行回报的权威账本。
 */
class PortfolioLedger {
 public:
  explicit PortfolioLedger(const dec_float& initial_cash = dec_float(0));

  /**
   * @brief 应用一条执行回报，重复 execution_id 会被幂等忽略。
   */
  domain::CommandResult apply(const domain::ExecutionReport& report);

  /**
   * @brief 设置指定订单冻结的资金或持仓。
   */
  domain::CommandResult reserve(const domain::OrderIntent& intent,
                                const dec_float& quantity);

  /**
   * @brief 释放指定订单冻结的资金或持仓。
   */
  domain::CommandResult release(const domain::OrderIntent& intent,
                                const dec_float& quantity);

  /// 返回当前不可变账本快照。
  domain::PortfolioSnapshotPtr snapshot() const;

  /// 设置当前估值价格并更新未实现盈亏。
  void markToMarket(const std::string& symbol, const dec_float& price,
                    int64_t timestamp_ms);

  /**
   * @brief 从 Legacy Gateway 同步现金状态。
   *
   * 仅限 `LegacyLedgerAdapter` 在初始化阶段调用；策略不得直接写入账本。
   */
  void syncCash(const dec_float& cash, const dec_float& frozen_cash,
                int64_t timestamp_ms = 0);

  /// 同 `syncCash`，同步指定品种持仓；仅限 `LegacyLedgerAdapter` 调用。
  void syncPosition(const domain::PositionSnapshot& position,
                    int64_t timestamp_ms = 0);

  /// 同 `syncCash`，整体同步持仓快照；仅限 `LegacyLedgerAdapter` 调用。
  void syncPositions(const std::vector<domain::PositionSnapshot>& positions,
                     int64_t timestamp_ms = 0);

  /**
   * @brief 账本是否已经被执行回报写入过。
   *
   * `LegacyLedgerAdapter` 用它判断能否采纳 Legacy 初始状态：
   * 一旦账本自身开始记账，就不再接受外部覆盖，避免出现状态回退。
   */
  bool hasExecutionUpdates() const { return m_execution_applied; }

 private:
  domain::PositionSnapshot* findPosition(const std::string& symbol);
  const domain::PositionSnapshot* findPosition(const std::string& symbol) const;
  void bumpVersion(int64_t timestamp_ms);

  /// 是否已经有执行回报写入过账本，用于阻止 Legacy 状态覆盖自身记账
  bool m_execution_applied{false};

  domain::PortfolioSnapshot m_snapshot;
  std::unordered_set<std::string> m_processed_executions;
};

}  // namespace core::portfolio

#endif  // QITRADER_CORE_PORTFOLIO_PORTFOLIO_LEDGER_H_
