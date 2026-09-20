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
}

void PerformanceAnalyzer::report(const dec_float& final_equity) const {
  auto total_return = calcTotalReturn(final_equity);
  auto max_drawdown = calcMaxDrawdown();
  auto sharpe = calcSharpeRatio();

  dec_float win_rate, profit_factor;
  int total_trades, winning_trades, losing_trades;
  calcTradeStats(win_rate, profit_factor, total_trades, winning_trades, losing_trades);

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
  LOG(INFO) << "--------------------------------------------";
  LOG(INFO) << fmt::format("总交易次数:   {}", total_trades);
  LOG(INFO) << fmt::format("盈利次数:     {}", winning_trades);
  LOG(INFO) << fmt::format("亏损次数:     {}", losing_trades);
  LOG(INFO) << fmt::format("胜率:         {}%",
                           dec_float(win_rate * 100).str(2, std::ios_base::fixed));
  LOG(INFO) << fmt::format("盈亏比:       {}", profit_factor.str(4, std::ios_base::fixed));
  LOG(INFO) << "============================================";
}

dec_float PerformanceAnalyzer::calcTotalReturn(const dec_float& final_equity) const {
  if (m_initial_capital == 0) return dec_float(0);
  return (final_equity - m_initial_capital) / m_initial_capital;
}

dec_float PerformanceAnalyzer::calcMaxDrawdown() const {
  if (m_equity_curve.empty()) return dec_float(0);

  dec_float peak = m_equity_curve[0].second;
  dec_float max_dd = dec_float(0);

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

dec_float PerformanceAnalyzer::calcSharpeRatio() const {
  if (m_equity_curve.size() < 2) return dec_float(0);

  // 计算每日收益率
  std::vector<dec_float> returns;
  for (size_t i = 1; i < m_equity_curve.size(); ++i) {
    if (m_equity_curve[i - 1].second > 0) {
      dec_float r = (m_equity_curve[i].second - m_equity_curve[i - 1].second) /
                    m_equity_curve[i - 1].second;
      returns.push_back(r);
    }
  }

  if (returns.empty()) return dec_float(0);

  // 平均收益率
  dec_float sum(0);
  for (const auto& r : returns) {
    sum += r;
  }
  dec_float mean = sum / dec_float(returns.size());

  // 标准差
  dec_float var_sum(0);
  for (const auto& r : returns) {
    dec_float diff = r - mean;
    var_sum += diff * diff;
  }
  dec_float stddev = boost::multiprecision::sqrt(var_sum / dec_float(returns.size()));

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

void PerformanceAnalyzer::calcTradeStats(dec_float& win_rate, dec_float& profit_factor,
                                         int& total_trades, int& winning_trades,
                                         int& losing_trades) const {
  total_trades = 0;
  winning_trades = 0;
  losing_trades = 0;
  dec_float total_profit(0);
  dec_float total_loss(0);

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

  win_rate = total_trades > 0 ? dec_float(winning_trades) / dec_float(total_trades) : dec_float(0);
  profit_factor = total_loss > 0 ? total_profit / total_loss : dec_float(0);
}

}  // namespace backtest::base
