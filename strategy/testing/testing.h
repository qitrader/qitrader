#ifndef QITRADER_STRATEGY_TESTING_TESTING_H_
#define QITRADER_STRATEGY_TESTING_TESTING_H_

/**
 * @file testing.h
 * @brief 测试策略：验证行情快照与订单计划链路。
 */

#include <string>

#include "base/strategy.h"

namespace strategy::testing {

/**
 * @brief 测试策略类。
 */
class Testing : public base::Strategy {
 public:
  /// 交易对由命令行 `--symbol` 注入，不再硬编码。
  explicit Testing(std::string symbol) : m_symbol(std::move(symbol)) {}
  ~Testing() override = default;

  /// 启动阶段只做日志，订单计划等首帧行情到达后再提交。
  asio::awaitable<void> run() override;

  /// 首帧行情到达后提交一笔市价买单计划，之后打印行情快照。
  void onMarket(const core::domain::MarketSnapshot& snapshot) override;

  /// 打印成交回报。
  void onExecution(const core::domain::ExecutionReport& report) override;

 private:
  std::string m_symbol;
  bool m_submitted{false};
};

}  // namespace strategy::testing

#endif  // QITRADER_STRATEGY_TESTING_TESTING_H_
