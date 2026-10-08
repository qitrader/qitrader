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
 * @brief 交易统计结果
 */
struct TradeStats {
  int total_fills{0};     ///< 成交笔数（买入 + 卖出）
  int closed_rounds{0};   ///< 已完成的买卖回合数
  int winning_trades{0};  ///< 盈利回合数
  int losing_trades{0};   ///< 亏损回合数
  int open_fills{0};      ///< 尚未平仓的成交笔数
  dec_float win_rate{0};  ///< 胜率（按已平仓回合计算）
  dec_float profit_factor{0};
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
   * @brief 裁剪指定时间之前、且已结算（买卖已配对完）的历史成交记录
   *
   * 长期运行时成交记录只增不减，内存单调增长，收尾报告的统计也会退化。
   * 这里只移除"已经结算"的前缀：即该位置之前买卖已全部配对完、
   * 不会再被后续成交引用的部分；未平仓的买入始终保留，剩余区间的
   * FIFO 配对结果也不改变。被裁剪部分的统计由累计计数器保存，
   * 报告的累计口径不随裁剪变化。
   *
   * @param timestamp_ms 保留窗口起点（毫秒），早于该时间的记录才可能被裁剪
   */
  void trimTradesBefore(int64_t timestamp_ms);

  /**
   * @brief 限制净值曲线的最大采样点数，超出后丢弃最旧的快照
   *
   * 被丢弃快照中出现过的最高净值与最大回撤会并入历史值，
   * 保证长期运行的最大回撤不会因裁剪被系统性低估。
   *
   * @param max_points 最大点数，0 表示不限制（回测默认不限制）
   */
  void setMaxEquityPoints(std::size_t max_points);

  /**
   * @brief 输出绩效报告到终端
   * @param final_equity 最终净值
   */
  void report(const dec_float& final_equity) const;

 private:
  /// 统计 [0, count) 区间内已平仓的回合，累加到裁剪计数器（口径同 calcTradeStats）
  void accumulateClosedStats(std::size_t count);

  /// 按点数上限丢弃最旧的净值快照，并把回撤信息并入历史值
  void applyEquityCap();

  /// 计算总收益率
  dec_float calcTotalReturn(const dec_float& final_equity) const;

  /// 计算最大回撤
  dec_float calcMaxDrawdown() const;

  /// 计算夏普比率（年化，无风险利率默认为 0）
  dec_float calcSharpeRatio() const;

  /// 计算成交与胜负统计
  TradeStats calcTradeStats() const;

  dec_float m_initial_capital;                   ///< 初始资金
  std::vector<TradeRecord> m_trades;             ///< 交易记录
  std::vector<std::pair<int64_t, dec_float>> m_equity_curve;  ///< 净值曲线 (timestamp, equity)

  std::size_t m_max_equity_points{0};       ///< 净值曲线最大点数，0 表示不限制
  dec_float m_historical_peak{0};           ///< 已裁剪快照中的最高净值
  dec_float m_historical_max_drawdown{0};   ///< 已裁剪快照中出现过的最大回撤

  // 已裁剪成交的累计统计：保证"总成交笔数"等报告口径不随回收变化
  int m_trimmed_fills{0};          ///< 已裁剪的成交笔数
  int m_trimmed_closed_rounds{0};  ///< 已裁剪部分统计出的平仓回合数
  int m_trimmed_winning{0};        ///< 已裁剪部分的盈利回合数
  int m_trimmed_losing{0};         ///< 已裁剪部分的亏损回合数
  dec_float m_trimmed_profit{0};   ///< 已裁剪部分的总盈利
  dec_float m_trimmed_loss{0};     ///< 已裁剪部分的总亏损
};

}  // namespace backtest::base

#endif  // QITRADER_BACKTEST_BASE_PERFORMANCE_ANALYZER_H_
