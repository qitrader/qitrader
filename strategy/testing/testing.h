#ifndef QITRADER_STRATEGY_TESTING_TESTING_H_
#define QITRADER_STRATEGY_TESTING_TESTING_H_

/**
 * @file testing.h
 * @brief 测试策略：验证行情快照与订单计划链路。
 */

#include "base/strategy.h"

namespace strategy::testing {

/**
 * @brief 测试策略类。
 */
class Testing : public base::Strategy {
 public:
  Testing() = default;
  ~Testing() override = default;

  /// 启动时提交一笔市价买单计划。
  asio::awaitable<void> run() override;

  /// 打印本帧行情快照。
  void onMarket(const core::domain::MarketSnapshot& snapshot) override;

  /// 打印成交回报。
  void onExecution(const core::domain::ExecutionReport& report) override;
};

}  // namespace strategy::testing

#endif  // QITRADER_STRATEGY_TESTING_TESTING_H_
