#ifndef QITRADER_STRATEGY_GRID_GRID_STRATEGY_H_
#define QITRADER_STRATEGY_GRID_GRID_STRATEGY_H_

/**
 * @file grid_strategy.h
 * @brief 网格交易策略：按目标订单计划收敛各层买卖挂单。
 */

#include <string>
#include <vector>

#include "base/strategy.h"

namespace strategy::grid {

/**
 * @brief 单个网格层级的决策状态。
 */
struct GridLevel {
  dec_float price;            ///< 网格价格
  bool has_position = false;  ///< 该层是否已占用（由成交回报更新）
};

/**
 * @brief 网格交易策略。
 */
class GridStrategy : public base::Strategy {
 public:
  /**
   * @param symbol          交易对
   * @param upper_price     网格上界
   * @param lower_price     网格下界
   * @param grid_count      网格数量
   * @param amount_per_grid 每格下单数量
   */
  GridStrategy(const std::string& symbol,
               dec_float upper_price,
               dec_float lower_price,
               int grid_count,
               dec_float amount_per_grid);
  ~GridStrategy() override = default;

  asio::awaitable<void> run() override;
  void onMarket(const core::domain::MarketSnapshot& snapshot) override;
  void onExecution(const core::domain::ExecutionReport& report) override;

 private:
  void buildGrid();
  void initOccupancy(const dec_float& current_price);
  void submitPlan(const dec_float& current_price);
  static int levelFromIntent(const std::string& intent_id);

  std::string m_symbol;
  dec_float m_upper_price;
  dec_float m_lower_price;
  int m_grid_count;
  dec_float m_amount_per_grid;
  dec_float m_grid_spacing;
  std::vector<GridLevel> m_levels;
  bool m_initialized = false;
  dec_float m_total_profit;
  int m_total_trades = 0;
};

}  // namespace strategy::grid

#endif  // QITRADER_STRATEGY_GRID_GRID_STRATEGY_H_
