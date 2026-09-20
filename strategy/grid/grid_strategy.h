#ifndef QITRADER_STRATEGY_GRID_GRID_STRATEGY_H_
#define QITRADER_STRATEGY_GRID_GRID_STRATEGY_H_

/**
 * @file grid_strategy.h
 * @brief 网格交易策略（Grid Trading Strategy）
 *
 * 核心思路：
 * - 在预设的价格区间内，按等间距（或等比例）布置若干买卖挂单
 * - 价格下跌触碰买单 → 成交后在上方挂卖单
 * - 价格上涨触碰卖单 → 成交后在下方挂买单
 * - 反复低买高卖，赚取网格利润
 *
 * 参数：
 * --grid-upper    网格上界价格
 * --grid-lower    网格下界价格
 * --grid-count    网格数量（将区间分成 N 个等间距格子）
 * --grid-amount   每格下单数量
 * --symbol        交易对（默认 BTC-USDT-SWAP）
 */

#include <map>
#include <string>
#include <vector>

#include "base/strategy.h"

namespace strategy {
namespace grid {

/**
 * @brief 单个网格层级
 */
struct GridLevel {
  dec_float price;                 ///< 网格价格
  std::string buy_order_id;       ///< 该层的买单 ID（空 = 无挂单）
  std::string sell_order_id;      ///< 该层的卖单 ID（空 = 无挂单）
  bool has_position = false;      ///< 该层是否已持仓（买单成交后为 true）
};

/**
 * @brief 网格交易策略类
 */
class GridStrategy : public base::Strategy {
 public:
  /**
   * @param engine        引擎指针
   * @param symbol        交易对
   * @param upper_price   网格上界
   * @param lower_price   网格下界
   * @param grid_count    网格数量
   * @param amount_per_grid 每格下单数量
   */
  GridStrategy(engine::EnginePtr engine,
               const std::string& symbol,
               dec_float upper_price,
               dec_float lower_price,
               int grid_count,
               dec_float amount_per_grid);
  ~GridStrategy();

  /// 策略运行入口
  asio::awaitable<void> run() override;

  /// 接收账户数据
  asio::awaitable<void> recv_account(engine::AccountDataPtr account) override;

  /// 接收持仓数据
  asio::awaitable<void> recv_position(engine::PositionDataPtr position) override;

  /// 接收订单簿数据
  asio::awaitable<void> recv_book(engine::BookPtr book) override;

  /// 接收 Tick 数据 — 网格主逻辑在此触发
  asio::awaitable<void> recv_tick(engine::TickDataPtr ticker) override;

  /// 接收 K 线数据
  asio::awaitable<void> recv_bar(engine::BarDataPtr bar) override;

  /// 接收订单回报
  asio::awaitable<void> recv_order(engine::OrderDataPtr order) override;

 private:
  /// 初始化网格层级
  void build_grid();

  /// 根据当前价格，在初始化时确定哪些层已"持仓"
  void init_grid_positions(dec_float current_price);

  /// 下买单
  asio::awaitable<void> place_buy_order(int level_index);

  /// 下卖单
  asio::awaitable<void> place_sell_order(int level_index);

  /// 根据最新价格检查并触发网格交易
  asio::awaitable<void> check_grid(dec_float current_price);

  /// 生成唯一订单 ID
  std::string generate_order_id();

  std::string m_symbol;            ///< 交易对
  dec_float m_upper_price;         ///< 网格上界
  dec_float m_lower_price;         ///< 网格下界
  int m_grid_count;                ///< 网格数量
  dec_float m_amount_per_grid;     ///< 每格数量
  dec_float m_grid_spacing;        ///< 网格间距

  std::vector<GridLevel> m_levels; ///< 所有网格层级（按价格从低到高）

  /// order_id → 网格层级索引的映射
  std::map<std::string, int> m_order_to_level;

  bool m_initialized = false;      ///< 是否已初始化网格
  int m_order_seq = 0;             ///< 订单序号

  dec_float m_total_profit;        ///< 累计网格利润
  int m_total_trades = 0;          ///< 累计成交次数
};

}  // namespace grid
}  // namespace strategy

#endif  // QITRADER_STRATEGY_GRID_GRID_STRATEGY_H_
