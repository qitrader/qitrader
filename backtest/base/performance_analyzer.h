#ifndef QITRADER_BACKTEST_BASE_PERFORMANCE_ANALYZER_H_
#define QITRADER_BACKTEST_BASE_PERFORMANCE_ANALYZER_H_

/**
 * @file performance_analyzer.h
 * @brief 回测绩效分析器
 *
 * 收集交易记录和净值变化，计算核心绩效指标：
 * - 总收益率（Total Return）
 * - 最大回撤（Max Drawdown）
 * - 夏普比率（Sharpe Ratio）
 * - 胜率（Win Rate）
 * - 盈亏比（Profit Factor）
 */

#include <string>
#include <vector>

#include "object.h"

namespace backtest::base {

/**
 * @brief 单笔交易记录（用于统计盈亏）
 */
struct TradeRecord {
  std::string symbol;           ///< 交易对
  engine::Direction direction;  ///< 方向
  dec_float price;              ///< 成交价格
  dec_float volume;             ///< 成交数量
  int64_t timestamp_ms;         ///< 成交时间
};

/**
 * @brief 绩效分析器
 */
class PerformanceAnalyzer {
 public:
  /**
   * @brief 构造函数
   * @param initial_capital 初始资金
   */
  explicit PerformanceAnalyzer(const dec_float& initial_capital);

  /**
   * @brief 记录一笔成交
   * @param trade 成交数据
   */
  void addTrade(std::shared_ptr<engine::TradeData> trade);

  /**
   * @brief 记录净值快照
   * @param equity 当前净值
   * @param timestamp_ms 时间戳
   */
  void addEquitySnapshot(const dec_float& equity, int64_t timestamp_ms);

  /**
   * @brief 输出绩效报告到终端
   * @param final_equity 最终净值
   */
  void report(const dec_float& final_equity) const;

 private:
  /// 计算总收益率
  dec_float calcTotalReturn(const dec_float& final_equity) const;

  /// 计算最大回撤
  dec_float calcMaxDrawdown() const;

  /// 计算夏普比率（年化，无风险利率默认为 0）
  dec_float calcSharpeRatio() const;

  /// 计算胜率和盈亏比
  void calcTradeStats(dec_float& win_rate, dec_float& profit_factor, int& total_trades,
                      int& winning_trades, int& losing_trades) const;

  dec_float m_initial_capital;                   ///< 初始资金
  std::vector<TradeRecord> m_trades;             ///< 交易记录
  std::vector<std::pair<int64_t, dec_float>> m_equity_curve;  ///< 净值曲线 (timestamp, equity)
};

}  // namespace backtest::base

#endif  // QITRADER_BACKTEST_BASE_PERFORMANCE_ANALYZER_H_
