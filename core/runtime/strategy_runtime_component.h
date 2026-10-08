#ifndef QITRADER_CORE_RUNTIME_STRATEGY_RUNTIME_COMPONENT_H_
#define QITRADER_CORE_RUNTIME_STRATEGY_RUNTIME_COMPONENT_H_

#include <memory>

#include "engine.h"
#include "strategy_runtime.h"

namespace core::runtime {

/**
 * @brief 将通用策略运行时接入 Engine 组件生命周期，并在行情事件后唤醒策略。
 *
 * 回测网关在撮合之后才同步投递行情事件，因此这里唤醒策略不会让本帧新单
 * 立刻成交。快照本身由 `MarketDataFeed` 在更早的 sink 中写入。
 */
class StrategyRuntimeComponent final
    : public engine::Component,
      public std::enable_shared_from_this<StrategyRuntimeComponent> {
 public:
  StrategyRuntimeComponent(engine::EnginePtr engine,
                           std::shared_ptr<StrategyRuntime> runtime)
      : m_engine(std::move(engine)), m_runtime(std::move(runtime)) {}

  /// 返回当前有效的引擎，引擎已释放时为空。
  std::shared_ptr<engine::Engine> engine() const { return m_engine.lock(); }

  /// 注册行情唤醒回调。
  asio::awaitable<void> init() override;

  /// 启动命令执行循环。
  asio::awaitable<void> run() override;

  /// 引擎停止前排空命令队列。
  asio::awaitable<void> shutdown() override;

 private:
  asio::awaitable<void> dispatchMarket();

  /// 引擎由 main 持有，这里只保存弱引用：
  /// 组件被引擎强持有，再强引用引擎会形成环，导致双方都无法析构。
  std::weak_ptr<engine::Engine> m_engine;
  std::shared_ptr<StrategyRuntime> m_runtime;
};

}  // namespace core::runtime

#endif  // QITRADER_CORE_RUNTIME_STRATEGY_RUNTIME_COMPONENT_H_
