#ifndef QITRADER_BACKTEST_BASE_BACKTEST_GATEWAY_H_
#define QITRADER_BACKTEST_BASE_BACKTEST_GATEWAY_H_

/**
 * @file backtest_gateway.h
 * @brief 回测网关
 *
 * 继承 Gateway 基类，替代实盘网关：
 * - 从 CSV 文件加载历史数据并按时间顺序回放
 * - 使用 MatchEngine 模拟订单撮合
 * - 维护模拟账户余额和持仓信息
 * - 回测结束后通过 PerformanceAnalyzer 输出绩效报告
 */

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/gateway.h"
#include "match/match_engine.h"
#include "base/performance_analyzer.h"

namespace backtest::base {

/**
 * @brief 回测网关，继承 market::base::Gateway
 *
 * 作为虚拟交易所网关注入引擎，策略代码无需修改即可在回测模式下运行。
 */
class BacktestGateway : public market::base::Gateway {
 public:
  /**
   * @brief 构造函数
   * @param engine 引擎指针
   * @param data_file CSV 数据文件路径
   * @param start_date 回测开始日期
   * @param end_date 回测结束日期
   * @param initial_capital 初始资金（默认 10000）
   */
  BacktestGateway(engine::EnginePtr engine, const std::string& data_file,
                  const std::string& start_date = "", const std::string& end_date = "",
                  const dec_float& initial_capital = dec_float(10000),
                  const dec_float& maker_fee_rate = dec_float("0.0008"),
                  const dec_float& taker_fee_rate = dec_float("0.001"));

  ~BacktestGateway() override = default;

  /// 回测主运行循环：按时间顺序回放历史数据
  asio::awaitable<void> run() override;

  // ========== Gateway 虚函数实现 ==========

  void connect() override {}
  void close() override {}
  void unsubscribe(const std::string& symbol) override;

  asio::awaitable<void> send_orders(engine::OrderDataPtr order) override;
  asio::awaitable<void> cancel_order(engine::OrderDataPtr order) override;
  asio::awaitable<void> query_account(engine::QueryAccountDataPtr data) override;
  asio::awaitable<void> query_position(engine::QueryPositionDataPtr data) override;
  asio::awaitable<void> query_order(engine::QueryOrderDataPtr data) override;
  asio::awaitable<void> subscribe_book(engine::SubscribeDataPtr data) override;
  asio::awaitable<void> subscribe_tick(engine::SubscribeDataPtr data) override;
  asio::awaitable<void> market_init() override;

  /// 行情转发钩子类型，参数为原始行情数据
  using TickSink = std::function<void(engine::TickDataPtr)>;
  using BarSink = std::function<void(std::shared_ptr<const engine::BarData>)>;

  /**
   * @brief 设置 Tick 转发钩子，由运行时同时驱动执行端口撮合与行情数据源
   * @param sink 参数为原始 Tick 数据
   */
  void set_tick_sink(TickSink sink) { m_tick_sink = std::move(sink); }

  /**
   * @brief 设置 K 线转发钩子，用途同 `set_tick_sink`
   * @param sink 参数为原始 K 线数据
   */
  void set_bar_sink(BarSink sink) { m_bar_sink = std::move(sink); }

  /**
   * @brief 暴露回测撮合器，供通用回测执行端口复用
   *
   * 复用后一次回测中只有一份订单簿、一份成交记录和一份绩效统计。
   */
  match::MatchEngine& match_engine() { return m_match_engine; }

 private:
  /// 处理成交事件，更新账户和持仓
  void onTradeEvent(std::shared_ptr<engine::OrderData> order,
                    std::shared_ptr<engine::TradeData> trade);

  /// 复制订单状态，避免异步事件看到后续撮合修改
  std::shared_ptr<engine::OrderData> cloneOrder(engine::OrderDataPtr order) const;

  /// 发送尚未通知策略的成交订单
  asio::awaitable<void> flushOrderNotifications();

  /// 计算当前净值
  dec_float calcEquity(const dec_float& current_price) const;

  /// 计算手续费：限价单按 maker 费率，市价单按 taker 费率
  dec_float calcFee(const dec_float& turnover, bool is_maker) const;

  std::string m_data_file;                    ///< CSV 数据文件路径
  std::string m_start_date;                   ///< 回测开始日期
  std::string m_end_date;                     ///< 回测结束日期

  dec_float m_initial_capital;                ///< 初始资金
  dec_float m_cash;                           ///< 当前现金
  dec_float m_reserved_cash{0};               ///< 限价买单冻结资金
  dec_float m_position_volume;                ///< 当前持仓数量
  dec_float m_reserved_position_volume{0};    ///< 限价卖单冻结持仓
  dec_float m_position_avg_price;             ///< 持仓均价
  std::string m_position_symbol;              ///< 持仓品种

  dec_float m_maker_fee_rate{0};              ///< 挂单成交（maker）手续费率
  dec_float m_taker_fee_rate{0};              ///< 吃单成交（taker）手续费率
  dec_float m_fees{0};                        ///< 累计手续费

  dec_float m_last_price;                     ///< 最新价格

  match::MatchEngine m_match_engine;          ///< 模拟撮合引擎
  PerformanceAnalyzer m_analyzer;             ///< 绩效分析器

  std::set<std::string> m_subscribed_symbols; ///< 已订阅的品种
  std::vector<std::shared_ptr<engine::OrderData>> m_pending_order_notifications;
  TickSink m_tick_sink;   ///< Tick 转发钩子，驱动运行时撮合与行情快照
  BarSink m_bar_sink;     ///< K 线转发钩子，用途同 Tick 钩子
};

}  // namespace backtest::base

#endif  // QITRADER_BACKTEST_BASE_BACKTEST_GATEWAY_H_
