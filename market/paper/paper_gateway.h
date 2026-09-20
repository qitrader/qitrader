#ifndef QITRADER_MARKET_PAPER_PAPER_GATEWAY_H_
#define QITRADER_MARKET_PAPER_PAPER_GATEWAY_H_

/**
 * @file paper_gateway.h
 * @brief 模拟交易网关（Paper Trading）
 *
 * 使用实盘行情数据 + 虚拟撮合引擎：
 * - 行情数据来自 OKX 实盘 WebSocket（实时 Tick / OrderBook）
 * - 下单、撤单、查询账户/持仓均在本地虚拟完成，不发送到交易所
 * - 使用 backtest::match::MatchEngine 进行模拟撮合
 * - 适合策略验证和调试，零资金风险
 *
 * 用法：
 *   ./qitrader --paper -c config.ini
 */

#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/gateway.h"
#include "base/performance_analyzer.h"
#include "match/match_engine.h"
#include "okx/okx_ws.h"
#include "utils/concurrent_map.hpp"

namespace market::paper {

using namespace market::okx;

/**
 * @brief 模拟交易网关
 *
 * 继承 Gateway 基类，行情通过 OKX WebSocket 获取（只连接公共频道），
 * 交易操作在本地虚拟撮合，不与交易所交互。
 */
class PaperGateway : public base::Gateway {
 public:
  /**
   * @brief 构造函数
   * @param engine 引擎指针
   * @param initial_capital 初始虚拟资金（默认 10000 USDT）
   * @param report_interval_s 账户摘要输出间隔（秒）
   * @param maker_fee_rate 挂单成交（maker）手续费率，0.0008 表示 0.08%
   * @param taker_fee_rate 吃单成交（taker）手续费率，0.001 表示 0.1%
   */
  PaperGateway(engine::EnginePtr engine,
               const dec_float& initial_capital = dec_float(10000),
               int report_interval_s = 60,
               const dec_float& maker_fee_rate = dec_float("0.0008"),
               const dec_float& taker_fee_rate = dec_float("0.001"));

  ~PaperGateway() override = default;

  /// 主运行循环：持续从 OKX WebSocket 接收实盘行情
  asio::awaitable<void> run() override;

  /// 优雅关闭：设置停止标志，退出 watch_public 循环
  asio::awaitable<void> shutdown() override;

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

  /// 开启行情录制；为空路径表示不录制。文件以追加方式打开，重启后继续录制。
  void setRecordPath(const std::string& path);

 private:
  /// 处理虚拟成交事件，更新账户和持仓
  void onTradeEvent(std::shared_ptr<engine::OrderData> order,
                    std::shared_ptr<engine::TradeData> trade);

  /// 处理 WebSocket 消息分发
  asio::awaitable<void> ws_deal(std::shared_ptr<OkxWs> ws);

  /// 处理行情数据
  asio::awaitable<void> deal_book(const std::string& symbol, const std::vector<WsBook>& msg);
  asio::awaitable<void> deal_tick(const std::string& symbol, const std::vector<WsTick>& msg);

  /// 持续监听公共 WebSocket（含自动重连）
  asio::awaitable<void> watch_public();

  /// 周期性输出账户摘要并记录净值快照，便于长期运行时观察效果
  asio::awaitable<void> reportLoop();

  /// 行情看门狗：检测行情长时间停滞并主动中断连接，触发重连
  asio::awaitable<void> watchdogLoop();

  /// 当前时间（毫秒），用于统计行情停滞时长
  static int64_t nowMs();

  /// 输出一行账户摘要并追加净值快照
  void reportSnapshot();

  /// 计算当前净值（现金 + 持仓市值）
  dec_float calcEquity() const;

  /// 计算一笔成交的手续费：限价单按 maker 费率，其余按 taker 费率
  dec_float calcFee(const dec_float& turnover, bool is_maker) const;

  /// 录制一条带盘口的行情，供离线重放训练使用。
  /// 只有 K 线没有盘口的数据无法训练做市策略，故无盘口的行情直接跳过。
  void recordTick(const std::shared_ptr<engine::TickData>& tick);

  // ========== 虚拟账户状态 ==========

  dec_float m_initial_capital;           ///< 初始资金
  dec_float m_cash;                      ///< 当前总现金
  dec_float m_reserved_cash{0};          ///< 限价买单冻结资金
  dec_float m_position_volume;           ///< 当前持仓数量
  dec_float m_reserved_position_volume{0}; ///< 限价卖单冻结持仓
  dec_float m_position_avg_price;        ///< 持仓均价
  std::string m_position_symbol;         ///< 持仓品种
  dec_float m_last_price;                ///< 最新价格

  dec_float m_maker_fee_rate{0};         ///< 挂单成交（maker）手续费率
  dec_float m_taker_fee_rate{0};         ///< 吃单成交（taker）手续费率
  dec_float m_fees{0};                   ///< 累计已支付手续费

  std::string m_record_path;             ///< 行情录制落盘路径，为空表示不录制
  std::unique_ptr<std::ofstream> m_record;
  /// 录制盘口档数，需与 CSV 扩展列及 CsvLoader 的解析保持一致
  static constexpr std::size_t kRecordBookLevels = 5;

  backtest::match::MatchEngine m_match_engine;  ///< 虚拟撮合引擎
  backtest::base::PerformanceAnalyzer m_analyzer;  ///< 绩效统计，与回测共用同一套指标

  std::set<std::string> m_subscribed_symbols;   ///< 已订阅的品种

  // ========== 运行统计 ==========

  int m_report_interval_s{60};   ///< 账户摘要输出间隔（秒）
  int m_trade_count{0};          ///< 成交笔数
  int m_order_count{0};          ///< 收到下单请求次数
  int m_cancel_count{0};         ///< 撤单次数
  int m_reject_count{0};         ///< 因冻结额度不足等被拒绝的下单次数
  int m_tick_count{0};           ///< 收到的行情消息数，用于判断连接是否健康
  std::atomic<int64_t> m_last_tick_time_ms{0};  ///< 最后一次收到行情的时间，供看门狗判定停滞
  int m_reconnect_count{0};      ///< WebSocket 重连次数
  int64_t m_start_time_ms{0};    ///< 启动时间，用于统计运行时长

  // ========== OKX WebSocket（仅公共频道） ==========

  struct SingleMarket {
    std::string symbol;
    engine::BookPtr last_book;
    engine::TickDataPtr last_tick;
  };

  std::shared_ptr<OkxWs> ws_public_;                    ///< 公共 WebSocket 连接
  ConcurrentMap<std::string, SingleMarket> markets_;     ///< 市场数据缓存

  std::atomic<bool> m_stopped{false};                    ///< 停止标志
  /// 成交后需要推送一次账户与持仓快照，避免 Legacy 侧状态陈旧
  std::atomic<bool> m_account_dirty{false};
  /// onTick 同步撮合出的成交订单。onTradeEvent 是同步回调无法推送事件，
  /// 先攒在这里，由行情处理协程在 onTick 之后统一回报给引擎。
  std::vector<std::shared_ptr<engine::OrderData>> m_pending_fills;
};

}  // namespace market::paper

#endif  // QITRADER_MARKET_PAPER_PAPER_GATEWAY_H_
