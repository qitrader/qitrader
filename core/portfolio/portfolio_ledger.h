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

 private:
  domain::PositionSnapshot* findPosition(const std::string& symbol);
  const domain::PositionSnapshot* findPosition(const std::string& symbol) const;
  void   bumpVersion(int64_t timestamp_ms);

  domain::PortfolioSnapshot m_snapshot;
  std::unordered_set<std::string> m_processed_executions;
};

}  // namespace core::portfolio

#endif  // QITRADER_CORE_PORTFOLIO_PORTFOLIO_LEDGER_H_
