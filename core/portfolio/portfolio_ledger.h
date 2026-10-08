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
   *
   * 市价单自身没有价格，必须按调用方传入的最新价估算名义价值；
   * 没有参考价时退回意图价格，避免按零价格冻结。
   */
  domain::CommandResult reserve(const domain::OrderIntent& intent,
                                const dec_float& quantity,
                                const dec_float& market_price = dec_float(0));

  /**
   * @brief 释放指定订单冻结的资金或持仓，参考价口径与 reserve 保持一致。
   */
  domain::CommandResult release(const domain::OrderIntent& intent,
                                const dec_float& quantity,
                                const dec_float& market_price = dec_float(0));

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
