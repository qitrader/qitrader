#ifndef QITRADER_CORE_RUNTIME_STRATEGY_RUNTIME_H_
#define QITRADER_CORE_RUNTIME_STRATEGY_RUNTIME_H_

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

#include "core/execution/execution_venue.h"
#include "core/execution/gateway_execution_venue_adapter.h"
#include "core/execution/order_manager.h"
#include "core/market/market_data_feed.h"
#include "core/portfolio/portfolio_ledger.h"
#include "core/risk/risk_manager.h"
#include <condition_variable>
#include <mutex>

#include "command_executor.h"
#include "strategy_context.h"

namespace core::runtime {

/// 用于协调引擎停止前等待运行命令处理完成。
struct DrainTracker {
  std::mutex mutex;
  std::condition_variable cv;
  std::size_t active{0};
};

/**
 * @brief 连接策略上下文、风控、订单管理、账本和执行端口的运行时。
 */
class StrategyRuntime : public std::enable_shared_from_this<StrategyRuntime> {
 public:
  StrategyRuntime(std::shared_ptr<portfolio::PortfolioLedger> ledger,
                  std::shared_ptr<risk::RiskManager> risk,
                  std::shared_ptr<execution::ExecutionVenue> venue,
                  std::shared_ptr<CommandQueue> queue);

  /// 提交订单计划，经过风控和计划差异计算后进入异步队列。
  asio::awaitable<domain::CommandResult> submit(const domain::OrderPlan& plan);

  /// 排空当前命令队列，通常与停止流程或按需调度配合使用。
  asio::awaitable<void> run();

  /// 设置用于按需调度排空协程的执行器。
  void setEngineExecutor(asio::any_io_executor executor) {
    m_engine_executor = std::move(executor);
  }

  /// 获取策略上下文。
  std::shared_ptr<StrategyContext> context() const { return m_context; }

  /// 获取当前执行端口。
  std::shared_ptr<execution::ExecutionVenue> venue() const { return m_venue; }

  /// 设置结构化运行时诊断回调。
  void setDiagnosticCallback(std::function<void(const domain::RuntimeDiagnostic&)> callback) {
    m_diagnostic_callback = std::move(callback);
  }

  /// 设置引擎运行时状态判断，避免引擎停止后继续下单或撤单。
  void setActiveChecker(std::function<bool()> checker) {
    m_active_checker = std::move(checker);
  }

  /**
   * @brief 接入行情数据源，使其快照直接进入策略上下文。
   *
   * 接入后策略不再需要在 `recv_tick` 中自行更新行情快照。
   */
  void setMarketFeed(std::shared_ptr<market::MarketDataFeed> feed);

  /// 获取当前行情数据源。
  std::shared_ptr<market::MarketDataFeed> marketFeed() const { return m_feed; }

  /// 通过行情数据源订阅行情，供纯运行时策略或交易环境使用。
  domain::CommandResult subscribeMarket(const domain::MarketSubscription& subscription);

  /**
   * @brief 检查当前是否没有待处理命令。
   */
  bool idle() const;

  /**
   * @brief 阻塞等待当前队列中的命令处理完成。
   * @param timeout_ms 最长等待时间，0 表示只检查一次
   * @return 队列是否已排空
   */
  bool waitIdle(uint32_t timeout_ms = 1000);

  /**
   * @brief 异步等待队列排空，供单线程事件循环安全使用。
   * @param timeout_ms 最长等待时间
   * @return 队列是否已排空
   */
  asio::awaitable<bool> waitIdleAsync(uint32_t timeout_ms = 1000);

  /// 异步等待执行端口中已投递命令完成，供单线程事件循环安全使用。
  asio::awaitable<bool> waitVenueIdleAsync(uint32_t timeout_ms = 1000);

  /// 等待命令排空后再关闭命令队列。
  asio::awaitable<void> stopAfterIdle(uint32_t timeout_ms = 1000);

  /// 停止接受新的订单计划并关闭执行端口，用于引擎停止前收口。
  void close() {
    m_closed.store(true);
    if (auto* gateway = dynamic_cast<execution::GatewayExecutionVenueAdapter*>(m_venue.get())) {
      gateway->close();
    }
  }

  /// 等待执行端口中已投递命令完成。
  bool waitVenueIdle(uint32_t timeout_ms) {
    auto* gateway = dynamic_cast<execution::GatewayExecutionVenueAdapter*>(m_venue.get());
    return gateway ? gateway->waitIdle(timeout_ms) : true;
  }

  /// 是否仍在接受订单计划。
  bool is_open() const { return !m_closed.load(); }

  /// 关闭命令队列。
  void stop() { close(); if (m_queue) m_queue->close(); }

 private:
  asio::awaitable<void> execute(const RuntimeCommand& command);
  void onExecution(const domain::ExecutionReport& report);
  void diagnostic(domain::DiagnosticSeverity severity, domain::ErrorCode code,
                  const std::string& plan_id, const std::string& order_id,
                  const std::string& message);
  std::string orderId(const domain::OrderIntent& intent,
                      const std::string& plan_id) const;
  static std::string planScope(const domain::OrderPlanDiff& diff);
  void finishCommand();

  std::shared_ptr<portfolio::PortfolioLedger> m_ledger;
  std::shared_ptr<risk::RiskManager> m_risk;
  std::shared_ptr<execution::ExecutionVenue> m_venue;
  std::shared_ptr<market::MarketDataFeed> m_feed;
  std::shared_ptr<CommandQueue> m_queue;
  std::shared_ptr<StrategyContext> m_context;
  execution::OrderManager m_orders;
  std::unique_ptr<CommandExecutor> m_executor;
  std::function<void(const domain::RuntimeDiagnostic&)> m_diagnostic_callback;
  std::function<bool()> m_active_checker;
  std::unordered_map<std::string, std::string> m_latest_plan_ids;
  std::shared_ptr<DrainTracker> m_drain;
  asio::any_io_executor m_engine_executor;
  std::atomic<bool> m_closed{false};
};

}  // namespace core::runtime

#endif  // QITRADER_CORE_RUNTIME_STRATEGY_RUNTIME_H_
