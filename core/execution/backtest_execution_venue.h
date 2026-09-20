#ifndef QITRADER_CORE_EXECUTION_BACKTEST_EXECUTION_VENUE_H_
#define QITRADER_CORE_EXECUTION_BACKTEST_EXECUTION_VENUE_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "backtest/match/match_engine.h"
#include "core/execution/execution_venue.h"

namespace core::execution {

/**
 * @brief 直接使用回测撮合器的执行端口。
 *
 * 该端口不经过 Engine 事件通道，也不依赖具体 Gateway，
 * 因此回测、离线训练和强化学习环境可以复用同一套订单计划与回报语义。
 *
 * 默认情况下端口自带一个独立撮合器，可离线运行；
 * 接入回测网关时应通过 `useMatchEngine` 复用网关的撮合器，
 * 保证一次回测中只有一份订单簿、一份成交记录和一份绩效统计。
 */
class BacktestExecutionVenue final : public ExecutionVenue {
 public:
  BacktestExecutionVenue();

  domain::CommandResult submit(const std::string& order_id,
                               const domain::OrderIntent& intent) override;
  domain::CommandResult cancel(const std::string& order_id) override;
  VenueCapabilities capabilities() const override;
  void setCallback(ExecutionCallback callback) override;

  /// 复用外部撮合器（通常是回测网关的撮合器）。
  void useMatchEngine(backtest::match::MatchEngine& engine) {
    m_match_engine = &engine;
  }

  /// 用最新价格撮合挂单，通常由回测行情回放驱动。
  void onMarketPrice(const std::string& symbol, const dec_float& price,
                     int64_t timestamp_ms);

  /// 设置市价单使用的当前价格。
  void setCurrentPrice(const dec_float& price) { m_current_price = price; }

  /// 设置手续费率。不设置时按零费率处理，训练出的策略将不会规避手续费。
  void setFeeRates(const dec_float& maker_fee_rate, const dec_float& taker_fee_rate) {
    m_maker_fee_rate = maker_fee_rate;
    m_taker_fee_rate = taker_fee_rate;
  }

 private:
  void emit(const domain::ExecutionReport& report);
  static domain::ExecutionEventType mapStatus(engine::OrderStatus status);
  backtest::match::MatchEngine& matchEngine() { return *m_match_engine; }

  /// 结算尚未处理的成交：更新持仓、归还卖单冻结额度并发出 FILL 回报。
  /// 用持久游标而不是每次进入时的快照，否则在两次行情之间产生的成交
  /// （市价单、即时撮合的限价单）会被永久跳过，冻结额度只增不减，
  /// 最终可用持仓被耗尽，所有卖单被拒。
  void settleTrades();

  /// 按成交金额与订单类型计算手续费（限价挂单 maker，市价 taker）
  dec_float calcFee(const dec_float& turnover, bool is_maker) const;

  /// 离线运行时的自有撮合器；复用外部撮合器后不再使用。
  std::unique_ptr<backtest::match::MatchEngine> m_owned_engine;
  backtest::match::MatchEngine* m_match_engine{nullptr};
  ExecutionCallback m_callback;
  std::unordered_map<std::string, domain::OrderIntent> m_intents;
  dec_float m_current_price{0};
  dec_float m_maker_fee_rate{0};
  dec_float m_taker_fee_rate{0};
  uint64_t m_report_counter{0};

  /// 各品种当前持仓。撮合器不跟踪持仓，缺少这层约束就会出现裸卖空，
  /// 持仓穿成巨额负数会让净值与奖励彻底失真。
  std::unordered_map<std::string, dec_float> m_positions;

  /// 已提交但未成交的卖出量，避免同一笔持仓被多张挂单重复占用。
  std::unordered_map<std::string, dec_float> m_reserved_sell;

  /// 已结算的成交数量（撮合器 trades() 的下标游标）。
  std::size_t m_trade_cursor{0};
};

}  // namespace core::execution

#endif  // QITRADER_CORE_EXECUTION_BACKTEST_EXECUTION_VENUE_H_
