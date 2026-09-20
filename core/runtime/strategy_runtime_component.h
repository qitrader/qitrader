#ifndef QITRADER_CORE_RUNTIME_STRATEGY_RUNTIME_COMPONENT_H_
#define QITRADER_CORE_RUNTIME_STRATEGY_RUNTIME_COMPONENT_H_

#include <memory>

#include "engine.h"
#include "strategy_runtime.h"

namespace core::runtime {

/**
 * @brief 将通用策略运行时接入旧 Engine 组件生命周期。
 *
 * run() 负责启动命令执行器；shutdown() 在引擎停止前异步排空命令队列。
 */
class StrategyRuntimeComponent final
    : public engine::Component,
      public std::enable_shared_from_this<StrategyRuntimeComponent> {
 public:
  StrategyRuntimeComponent(engine::EnginePtr engine,
                           std::shared_ptr<StrategyRuntime> runtime)
      : m_engine(std::move(engine)), m_runtime(std::move(runtime)) {}

  /// 组件初始化，仅输出运行日志。
  asio::awaitable<void> init() override;

  /// 启动命令执行循环。
  asio::awaitable<void> run() override;

  /// 引擎停止前排空命令队列，避免停止阶段继续下单或撤单。
  asio::awaitable<void> shutdown() override;

 private:
  engine::EnginePtr m_engine;
  std::shared_ptr<StrategyRuntime> m_runtime;
};

}  // namespace core::runtime

#endif  // QITRADER_CORE_RUNTIME_STRATEGY_RUNTIME_COMPONENT_H_
