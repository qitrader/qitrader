#ifndef QITRADER_STRATEGY_MULTILEVEL_MULTILEVEL_STRATEGY_H_
#define QITRADER_STRATEGY_MULTILEVEL_MULTILEVEL_STRATEGY_H_

#include <cstdint>
#include <string>
#include <vector>

#include "actor_critic.h"
#include "base/strategy.h"

namespace strategy::multilevel {

/**
 * @brief 多层级强化学习做市策略配置。
 */
struct MultiLevelConfig {
  std::string symbol;
  int levels{3};
  int order_budget{20};
  double order_size{1.0};
  double inventory_limit{20.0};
  int64_t decision_interval_ms{30000};
  double inventory_penalty{0.01};
  double learning_rate{0.0005};
  double exploration{0.05};
  /// 模型落盘路径；非空时启动时加载、关闭时保存，使在线学习成果可累积
  std::string model_path;
  /// 每训练多少步周期性落盘一次；<=0 表示只在关闭时保存。
  /// 长期运行的进程若只在退出时保存，被 kill -9 或机器重启会让全部在线学习成果丢失。
  int model_save_interval_steps{500};
  /// 单边报价至少偏离保留价的 bps 数。做市往返成本是双边手续费，
  /// 若报价贴着盘口（ETH 实测价差仅 3 bps）而双边费率 16 bps，则每笔往返必亏。
  /// 设为费率量级（如 8~12）才能让单笔往返的价差覆盖手续费。0 表示不约束。
  double min_half_spread_bps{0.0};
  /// 是否允许动作空间里的市价单。市价单是 taker，单次就吃掉 10 bps，
  /// 在价差 3 bps 的市场里没有任何正期望场景，默认关闭。
  bool allow_market_orders{false};
  /// 库存偏斜强度：每单位库存比例（inventory / inventory_limit）把保留价推移多少 bps。
  /// 持多头时保留价下移，双边报价随之下移，卖出更易成交、买入更保守，
  /// 从而把库存拉回中性，避免单边囤积后被迫止损。
  double inventory_skew_bps{0.0};
};

/**
 * @brief 基于 Logistic-Normal Actor-Critic 的多层级做市策略。
 *
 * 策略使用订单簿失衡、价差、短期收益、自身库存和变长挂单集合聚合特征，
 * 在多个买卖价格层级之间分配有限订单预算。没有订单簿数据时会从 Tick
 * 价格构造保守的合成盘口，因此可以直接用于当前 CSV 回测入口。
 */
class MultiLevelMarketMakingStrategy : public base::Strategy {
 public:
  MultiLevelMarketMakingStrategy(engine::EnginePtr engine, MultiLevelConfig config);
  ~MultiLevelMarketMakingStrategy() override = default;

  asio::awaitable<void> run() override;
  /// 关闭时落盘模型，保证本次在线学习的成果不会随进程退出丢失
  asio::awaitable<void> shutdown() override;
  asio::awaitable<void> recv_account(engine::AccountDataPtr account) override;
  asio::awaitable<void> recv_position(engine::PositionDataPtr position) override;
  asio::awaitable<void> recv_book(engine::BookPtr book) override;
  asio::awaitable<void> recv_tick(engine::TickDataPtr ticker) override;
  asio::awaitable<void> recv_bar(engine::BarDataPtr bar) override;
  asio::awaitable<void> recv_order(engine::OrderDataPtr order) override;

 private:
  std::vector<double> makeObservation() const;
  engine::BookPtr makeSyntheticBook(const dec_float& price, int64_t timestamp_ms) const;
  double currentMidPrice() const;
  double tickSize() const;
  /// 从统一账本读取可用现金；策略不再维护本地资金状态。
  double effectiveCash() const;
  /// 从统一账本读取净库存；策略不再维护本地持仓状态。
  double effectiveInventory() const;
  /// 从统一账本读取可用于卖出的库存（净库存扣除已被挂单冻结的部分）。
  double availableInventory() const;
  void updateTransition(const std::vector<double>& observation);
  /// 加载模型；文件不存在时保持初始权重并从零开始学习
  bool loadModel();
  /// 保存模型到配置路径
  bool saveModel() const;
  std::vector<int> allocateLots(const std::vector<double>& action) const;
  asio::awaitable<void> reconfigureOrders(const std::vector<double>& observation);
  std::string generateOrderId();

  MultiLevelConfig m_config;
  ActorCritic m_policy;
  engine::BookPtr m_book;
  double m_last_price{0.0};
  double m_feature_mid{0.0};
  /// 上一次转移时的现金与库存，用于计算 Actor-Critic 奖励，不是账本权威值
  double m_previous_cash{0.0};
  double m_previous_inventory{0.0};
  double m_previous_mid{0.0};
  double m_tick_size{0.0};
  int64_t m_current_timestamp{0};
  int64_t m_last_decision_timestamp{0};
  uint64_t m_order_sequence{0};
  bool m_has_transition{false};

  /// 训练观测：累计步数与奖励，用于判断策略是否真的在学
  std::size_t m_train_steps{0};
  double m_train_reward_sum{0.0};
  double m_train_reward_window{0.0};
  std::size_t m_train_window_count{0};

  std::vector<double> m_previous_observation;
  std::vector<double> m_previous_action;
};

}  // namespace strategy::multilevel

#endif  // QITRADER_STRATEGY_MULTILEVEL_MULTILEVEL_STRATEGY_H_
