#ifndef QITRADER_STRATEGY_BASE_STRATEGY_H_
#define QITRADER_STRATEGY_BASE_STRATEGY_H_

/**
 * @file strategy.h
 * @brief 交易策略基类：只依赖领域快照与订单计划。
 */

#include "engine.h"
#include "core/runtime/strategy_context.h"

namespace core::runtime {
class StrategyRuntime;
}

namespace strategy::base {

/**
 * @brief 策略基类。
 *
 * 生命周期仍挂在 Engine 组件上；行情与成交全部通过领域类型进入策略。
 */
class Strategy : public std::enable_shared_from_this<Strategy>, public engine::Component {
 public:
  Strategy() = default;
  ~Strategy() override = default;

  asio::awaitable<void> init() override;

  /**
   * @brief 注入通用策略运行时上下文。
   * @param context 策略运行时上下文
   * @param runtime 可选通用运行时，用于注册行情/成交回调并在停止前排空
   */
  void set_runtime_context(std::shared_ptr<core::runtime::StrategyContext> context,
                           std::shared_ptr<core::runtime::StrategyRuntime> runtime = {});

  /// 获取通用策略运行时上下文。
  std::shared_ptr<core::runtime::StrategyContext> runtime_context() const {
    return m_runtime_context;
  }

  /**
   * @brief 本帧行情快照就绪后的决策入口。
   *
   * 回测中该回调发生在本帧撮合之后，不得等待执行结果。
   */
  virtual void onMarket(const core::domain::MarketSnapshot& snapshot) { (void)snapshot; }

  /**
   * @brief 标准化执行回报。默认忽略，策略可用它更新决策状态。
   */
  virtual void onExecution(const core::domain::ExecutionReport& report) { (void)report; }

 private:
  std::shared_ptr<core::runtime::StrategyContext> m_runtime_context;
  std::shared_ptr<core::runtime::StrategyRuntime> m_runtime;
};

}  // namespace strategy::base

#endif  // QITRADER_STRATEGY_BASE_STRATEGY_H_
