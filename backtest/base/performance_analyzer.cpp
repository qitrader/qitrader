#include "performance_analyzer.h"

#include <glog/logging.h>

#include <deque>
#include <map>

namespace backtest::base {

PerformanceAnalyzer::PerformanceAnalyzer(const dec_float& initial_capital)
    : m_initial_capital(initial_capital) {}

void PerformanceAnalyzer::addTrade(std::shared_ptr<engine::TradeData> trade) {
  if (!trade) return;
  TradeRecord record;
  record.symbol = trade->symbol;
  record.direction = trade->direction;
  record.price = trade->price;
  record.volume = trade->volume;
  record.timestamp_ms = trade->timestamp_ms;
  m_trades.push_back(record);
}

void PerformanceAnalyzer::addEquitySnapshot(const dec_float& equity, int64_t timestamp_ms) {
  m_equity_curve.push_back({timestamp_ms, equity});
  applyEquityCap();
}

void PerformanceAnalyzer::setMaxEquityPoints(std::size_t max_points) {
  m_max_equity_points = max_points;
  applyEquityCap();
}

void PerformanceAnalyzer::applyEquityCap() {
  if (m_max_equity_points == 0 || m_equity_curve.size() <= m_max_equity_points) return;

  const std::size_t drop = m_equity_curve.size() - m_max_equity_points;
  // 被丢弃的快照仍要参与最大回撤统计：先把它们的最高净值与回撤并入历史值，
  // 否则长期运行的最大回撤会随着裁剪被系统性低估。
  for (std::size_t i = 0; i < drop; ++i) {
    const dec_float& equity = m_equity_curve[i].second;
    if (equity > m_historical_peak) m_historical_peak = equity;
    if (m_historical_peak > 0) {
      const dec_float dd = (m_historical_peak - equity) / m_historical_peak;
      if (dd > m_historical_max_drawdown) m_historical_max_drawdown = dd;
    }
  }
  m_equity_curve.erase(m_equity_curve.begin(),
                       m_equity_curve.begin() + static_cast<std::ptrdiff_t>(drop));
}

void PerformanceAnalyzer::trimTradesBefore(int64_t timestamp_ms) {
  if (m_trades.empty() || timestamp_ms <= 0) return;

  // 先按与 calcTradeStats 相同的 FIFO 口径复算一遍，找出可以安全裁剪的前缀：
  // 只有"该笔之后没有任何未平仓残量"的位置才是完整回合的边界。
  std::map<std::string, std::deque<TradeRecord>> open_positions;
  std::size_t scanned = 0;
  std::size_t safe = 0;  // 最后一个"全部平仓"位置之前的元素个数
  for (const auto& trade : m_trades) {
    if (trade.timestamp_ms >= timestamp_ms) break;  // 成交按到达顺序追加，可直接停止

    if (trade.direction == engine::Direction::BUY) {
      open_positions[trade.symbol].push_back(trade);
    } else {
      auto position_it = open_positions.find(trade.symbol);
      if (position_it != open_positions.end()) {
        auto& positions = position_it->second;
        dec_float remaining = trade.volume;
        while (remaining > 0 && !positions.empty()) {
          auto& open = positions.front();
          const dec_float matched_volume =
              open.volume < remaining ? open.volume : remaining;
          open.volume -= matched_volume;
          remaining -= matched_volume;
          if (open.volume <= 0) positions.pop_front();
        }
      }
    }
    ++scanned;

    bool all_flat = true;
    for (const auto& [symbol, positions] : open_positions) {
      if (!positions.empty()) {
        all_flat = false;
        break;
      }
    }
    if (all_flat) safe = scanned;
  }

  // 窗口之前仍有未平仓的买入，裁剪会丢掉它们与后续卖出的配对，故整体放弃。
  if (safe == 0) return;

  accumulateClosedStats(safe);
  m_trimmed_fills += static_cast<int>(safe);
  m_trades.erase(m_trades.begin(), m_trades.begin() + static_cast<std::ptrdiff_t>(safe));
}

void PerformanceAnalyzer::accumulateClosedStats(std::size_t count) {
  std::map<std::string, std::deque<TradeRecord>> open_positions;
  for (std::size_t i = 0; i < count && i < m_trades.size(); ++i) {
    const TradeRecord& trade = m_trades[i];
    if (trade.direction == engine::Direction::BUY) {
      open_positions[trade.symbol].push_back(trade);
      continue;
    }

    auto position_it = open_positions.find(trade.symbol);
    if (position_it == open_positions.end()) continue;
    auto& positions = position_it->second;
    dec_float remaining = trade.volume;

    while (remaining > 0 && !positions.empty()) {
      auto& open = positions.front();
      const dec_float matched_volume =
          open.volume < remaining ? open.volume : remaining;
      const dec_float pnl = (trade.price - open.price) * matched_volume;

      ++m_trimmed_closed_rounds;
      if (pnl > 0) {
        ++m_trimmed_winning;
        m_trimmed_profit += pnl;
      } else if (pnl < 0) {
        ++m_trimmed_losing;
        m_trimmed_loss += boost::multiprecision::abs(pnl);
      }

      open.volume -= matched_volume;
      remaining -= matched_volume;
      if (open.volume <= 0) positions.pop_front();
    }
  }
}

void PerformanceAnalyzer::report(const dec_float& final_equity) const {
  auto total_return = calcTotalReturn(final_equity);
  auto max_drawdown = calcMaxDrawdown();
  auto sharpe = calcSharpeRatio();
  auto raw_sharpe = calcRawSharpeRatio();

  const TradeStats stats = calcTradeStats();

  LOG(INFO) << "============================================";
  LOG(INFO) << "           回测绩效报告";
  LOG(INFO) << "============================================";
  LOG(INFO) << fmt::format("初始资金:     {}", m_initial_capital.str(2, std::ios_base::fixed));
  LOG(INFO) << fmt::format("最终净值:     {}", final_equity.str(2, std::ios_base::fixed));
  LOG(INFO) << fmt::format("总收益率:     {}%",
                           dec_float(total_return * 100).str(2, std::ios_base::fixed));
  LOG(INFO) << fmt::format("最大回撤:     {}%",
                           dec_float(max_drawdown * 100).str(2, std::ios_base::fixed));
  LOG(INFO) << fmt::format("夏普比率:     {}", sharpe.str(4, std::ios_base::fixed));
  // 长跑快照间隔只有 60s，年化因子约 725，年化值量级失真；
  // 同时给出未年化值，便于判断策略本身的收益/波动比。
  LOG(INFO) << fmt::format("夏普(未年化): {}", raw_sharpe.str(6, std::ios_base::fixed));
  LOG(INFO) << "--------------------------------------------";
  LOG(INFO) << fmt::format("总成交笔数:   {}", stats.total_fills);
  LOG(INFO) << fmt::format("平仓回合:     {}", stats.closed_rounds);
  LOG(INFO) << fmt::format("盈利次数:     {}", stats.winning_trades);
  LOG(INFO) << fmt::format("亏损次数:     {}", stats.losing_trades);
  LOG(INFO) << fmt::format("未平仓笔数:   {}", stats.open_fills);
  LOG(INFO) << fmt::format("胜率:         {}%",
                           dec_float(stats.win_rate * 100).str(2, std::ios_base::fixed));
  LOG(INFO) << fmt::format("盈亏比:       {}", stats.profit_factor.str(4, std::ios_base::fixed));
  LOG(INFO) << "============================================";
}

dec_float PerformanceAnalyzer::calcTotalReturn(const dec_float& final_equity) const {
  if (m_initial_capital == 0) return dec_float(0);
  return (final_equity - m_initial_capital) / m_initial_capital;
}

dec_float PerformanceAnalyzer::calcMaxDrawdown() const {
  if (m_equity_curve.empty()) return m_historical_max_drawdown;

  // 已裁剪快照里出现过的最高净值要继续作为回撤基准，
  // 否则净值创新高后的回撤会因为基准被裁掉而算不出来。
  dec_float peak = m_historical_peak > m_equity_curve[0].second
      ? m_historical_peak : m_equity_curve[0].second;
  dec_float max_dd = m_historical_max_drawdown;

  for (const auto& [ts, equity] : m_equity_curve) {
    if (equity > peak) {
      peak = equity;
    }
    if (peak > 0) {
      dec_float dd = (peak - equity) / peak;
      if (dd > max_dd) {
        max_dd = dd;
      }
    }
  }

  return max_dd;
}

dec_float PerformanceAnalyzer::calcRawSharpeRatio() const {
  dec_float mean(0);
  dec_float stddev(0);
  if (!calcReturnStats(mean, stddev)) return dec_float(0);
  if (stddev == 0) return dec_float(0);
  return mean / stddev;
}

bool PerformanceAnalyzer::calcReturnStats(dec_float& mean, dec_float& stddev) const {
  mean = dec_float(0);
  stddev = dec_float(0);
  if (m_equity_curve.size() < 2) return false;

  // 计算每个净值快照之间的收益率
  std::vector<dec_float> returns;
  for (size_t i = 1; i < m_equity_curve.size(); ++i) {
    if (m_equity_curve[i - 1].second > 0) {
      dec_float r = (m_equity_curve[i].second - m_equity_curve[i - 1].second) /
                    m_equity_curve[i - 1].second;
      returns.push_back(r);
    }
  }

  if (returns.empty()) return false;

  // 平均收益率
  dec_float sum(0);
  for (const auto& r : returns) {
    sum += r;
  }
  mean = sum / dec_float(returns.size());

  // 标准差
  dec_float var_sum(0);
  for (const auto& r : returns) {
    dec_float diff = r - mean;
    var_sum += diff * diff;
  }
  stddev = boost::multiprecision::sqrt(var_sum / dec_float(returns.size()));
  return true;
}

dec_float PerformanceAnalyzer::calcSharpeRatio() const {
  dec_float mean(0);
  dec_float stddev(0);
  if (!calcReturnStats(mean, stddev)) return dec_float(0);
  if (stddev == 0) return dec_float(0);

  // 根据净值快照的实际平均时间间隔年化，而不是假设每个快照都是日收益率。
  dec_float total_interval_seconds(0);
  int64_t interval_count = 0;
  for (size_t i = 1; i < m_equity_curve.size(); ++i) {
    const auto interval_ms = m_equity_curve[i].first - m_equity_curve[i - 1].first;
    if (interval_ms > 0) {
      total_interval_seconds += dec_float(interval_ms) / dec_float(1000);
      ++interval_count;
    }
  }
  if (interval_count == 0) return dec_float(0);

  const dec_float average_interval_seconds =
      total_interval_seconds / dec_float(interval_count);
  const dec_float periods_per_year = dec_float(365.25 * 24 * 60 * 60) /
                                     average_interval_seconds;
  return (mean / stddev) * boost::multiprecision::sqrt(periods_per_year);
}

TradeStats PerformanceAnalyzer::calcTradeStats() const {
  TradeStats stats;
  // 累计口径必须包含已裁剪的历史成交：裁剪只是回收内存，不代表这些成交没发生过。
  stats.total_fills = m_trimmed_fills + static_cast<int>(m_trades.size());
  int total_trades = m_trimmed_closed_rounds;
  int winning_trades = m_trimmed_winning;
  int losing_trades = m_trimmed_losing;
  dec_float total_profit = m_trimmed_profit;
  dec_float total_loss = m_trimmed_loss;

  // 按品种使用 FIFO 配对买卖，支持部分成交和不同成交数量。
  std::map<std::string, std::deque<TradeRecord>> open_positions;
  for (const auto& trade : m_trades) {
    if (trade.direction == engine::Direction::BUY) {
      open_positions[trade.symbol].push_back(trade);
      continue;
    }

    auto position_it = open_positions.find(trade.symbol);
    if (position_it == open_positions.end()) continue;
    auto& positions = position_it->second;
    dec_float remaining = trade.volume;

    while (remaining > 0 && !positions.empty()) {
      auto& open = positions.front();
      const dec_float matched_volume =
          open.volume < remaining ? open.volume : remaining;
      const dec_float pnl = (trade.price - open.price) * matched_volume;

      ++total_trades;
      if (pnl > 0) {
        ++winning_trades;
        total_profit += pnl;
      } else if (pnl < 0) {
        ++losing_trades;
        total_loss += boost::multiprecision::abs(pnl);
      }

      open.volume -= matched_volume;
      remaining -= matched_volume;
      if (open.volume <= 0) positions.pop_front();
    }
  }

  stats.closed_rounds = total_trades;
  // 未平仓的买入不计入回合，单独列出，避免"只买未卖"的回测被统计成 0 笔交易。
  for (const auto& [symbol, positions] : open_positions) {
    for (const auto& open : positions) {
      if (open.volume > 0) ++stats.open_fills;
    }
  }
  stats.winning_trades = winning_trades;
  stats.losing_trades = losing_trades;
  stats.win_rate = total_trades > 0 ? dec_float(winning_trades) / dec_float(total_trades)
                                    : dec_float(0);
  stats.profit_factor = total_loss > 0 ? total_profit / total_loss : dec_float(0);
  return stats;
}

}  // namespace backtest::base
