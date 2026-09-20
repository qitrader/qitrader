#ifndef QITRADER_CORE_ENVIRONMENT_TRADING_ENVIRONMENT_H_
#define QITRADER_CORE_ENVIRONMENT_TRADING_ENVIRONMENT_H_

#include <string>
#include <vector>

#include "core/domain/types.h"

namespace core::environment {

struct Observation {
  int64_t timestamp_ms{0};
  domain::MarketSnapshot market;
  domain::PortfolioSnapshot portfolio;
  std::vector<double> features;
};

struct Action {
  std::string action_id;
  std::vector<double> values;
};

/**
 * @brief 单步奖励及其成本分解。
 *
 * `value` 是强化学习直接使用的标量奖励；其余字段是 `value` 的成本分解，
 * 便于分析手续费和滑点的影响，不参与重复扣减。
 */
struct Reward {
  double value{0};         ///< 综合奖励，等于 pnl 减去库存惩罚
  double pnl{0};           ///< 权益变化，已按实际成交价和手续费计算
  double inventory_penalty{0};  ///< 库存偏离惩罚
  double transaction_cost{0};   ///< 本步产生的手续费（已包含在 pnl 中）
  double slippage{0};           ///< 本步产生的滑点成本（已包含在 pnl 中）
};

struct Transition {
  Observation observation;
  Action action;
  Reward reward;
  Observation next_observation;
  bool terminal{false};
  int64_t timestamp_ms{0};
};

/**
 * @brief 可插拔交易环境，统一离线回测和实时策略的状态转移接口。
 */
class TradingEnvironment {
 public:
  virtual ~TradingEnvironment() = default;

  /// 重置环境并返回初始观测。
  virtual Observation reset() = 0;

  /// 执行动作并推进环境。
  virtual Transition step(const Action& action) = 0;
};

}  // namespace core::environment

#endif  // QITRADER_CORE_ENVIRONMENT_TRADING_ENVIRONMENT_H_
