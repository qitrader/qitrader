#include "strategy_runtime.h"

#include <fmt/format.h>
#include <glog/logging.h>

#include <chrono>
#include <utility>

#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>

namespace core::runtime {

StrategyRuntime::StrategyRuntime(
    std::shared_ptr<portfolio::PortfolioLedger> ledger,
    std::shared_ptr<risk::RiskManager> risk,
    std::shared_ptr<execution::ExecutionVenue> venue,
    std::shared_ptr<CommandQueue> queue)
    : m_ledger(std::move(ledger)),
      m_risk(std::move(risk)),
      m_venue(std::move(venue)),
      m_queue(std::move(queue)),
      m_drain(std::make_shared<DrainTracker>()) {
  m_context = std::make_shared<StrategyContext>(
      m_ledger, m_risk,
      [this](const domain::OrderPlan& plan) { return submit(plan); },
      [this](const domain::OrderPlan& plan) -> asio::awaitable<domain::CommandResult> {
        co_return submit(plan);
      },
      [this](const std::string& strategy_id) {
        return m_orders.activeOrders(strategy_id);
      });
  m_executor = std::make_unique<CommandExecutor>(
      m_queue, [this](const RuntimeCommand& command) { return execute(command); });
  if (m_queue) {
    m_queue->setDrainScheduler([this]() {
      auto executor = m_engine_executor;
      asio::co_spawn(executor, [this]() -> asio::awaitable<void> {
        if (m_executor) co_await m_executor->drain();
      }, asio::detached);
    });
  }
  if (m_venue) {
    m_venue->setCallback([this](const domain::ExecutionReport& report) {
      onExecution(report);
    });
  }
}

domain::CommandResult StrategyRuntime::submit(const domain::OrderPlan& plan) {
  if (m_closed.load()) {
    return {false, {domain::ErrorCode::QUEUE_FULL, "strategy runtime is closed"}, plan.plan_id};
  }
  if (!m_queue || !m_risk || !m_ledger) {
    diagnostic(domain::DiagnosticSeverity::ERROR, domain::ErrorCode::INTERNAL_ERROR,
               plan.plan_id, {}, "strategy runtime is incomplete");
    return {false, {domain::ErrorCode::INTERNAL_ERROR, "strategy runtime is incomplete"},
            plan.plan_id};
  }
  const auto decision = m_risk->check(plan, *m_ledger->snapshot());
  if (!decision.accepted) {
    diagnostic(domain::DiagnosticSeverity::WARNING, decision.error.code,
               plan.plan_id, {}, decision.error.message);
    return {false, decision.error, plan.plan_id};
  }
  const auto diff = m_orders.reconcile(decision.plan);
  m_latest_plan_ids[plan.strategy_id + "|" + plan.symbol] = plan.plan_id;
  if (diff.operations.empty()) return {true, {}, plan.plan_id};
  RuntimeCommand command;
  command.command_id = plan.plan_id;
  command.diff = diff;
  {
    std::lock_guard<std::mutex> lock(m_drain->mutex);
    ++m_drain->active;
  }
  const auto result = m_queue->push(std::move(command));
  if (!result.accepted) {
    std::lock_guard<std::mutex> lock(m_drain->mutex);
    if (m_drain->active > 0) --m_drain->active;
    m_drain->cv.notify_all();
  }
  return result;
}

void StrategyRuntime::notifyMarket(const domain::MarketSnapshot& snapshot) {
  if (m_market_handler) m_market_handler(snapshot);
}

bool StrategyRuntime::idle() const {
  std::lock_guard<std::mutex> lock(m_drain->mutex);
  return m_drain->active == 0;
}

bool StrategyRuntime::waitIdle(uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(m_drain->mutex);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (m_drain->active > 0) {
    if (timeout_ms == 0) return false;
    if (m_drain->cv.wait_until(lock, deadline) == std::cv_status::timeout) {
      return m_drain->active == 0;
    }
  }
  return true;
}

asio::awaitable<bool> StrategyRuntime::waitIdleAsync(uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!idle()) {
    if (std::chrono::steady_clock::now() >= deadline) co_return idle();
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(std::chrono::milliseconds(1));
    co_await timer.async_wait(asio::use_awaitable);
  }
  co_return true;
}

asio::awaitable<bool> StrategyRuntime::waitVenueIdleAsync(uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!waitVenueIdle(0)) {
    if (std::chrono::steady_clock::now() >= deadline) co_return waitVenueIdle(0);
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(std::chrono::milliseconds(1));
    co_await timer.async_wait(asio::use_awaitable);
  }
  co_return true;
}

asio::awaitable<void> StrategyRuntime::stopAfterIdle(uint32_t timeout_ms) {
  const bool drained = co_await waitIdleAsync(timeout_ms);
  if (!drained) {
    LOG(WARNING) << "运行时命令队列在停止前未完全排空";
  }
  // 必须等执行端口的在途派发结束再停止 io_context：
  // 这些协程挂在引擎事件通道上，被强制销毁会破坏调度器的操作队列。
  const bool venue_drained = co_await waitVenueIdleAsync(timeout_ms);
  if (!venue_drained) {
    LOG(WARNING) << "运行时执行端口在停止前仍有未完成的派发";
  }
  stop();
}

asio::awaitable<void> StrategyRuntime::run() {
  if (m_executor) co_await m_executor->drain();
}

asio::awaitable<void> StrategyRuntime::execute(const RuntimeCommand& command) {
  if (!m_venue) {
    finishCommand();
    co_return;
  }
  if (m_closed.load() || (m_active_checker && !m_active_checker())) {
    diagnostic(domain::DiagnosticSeverity::WARNING, domain::ErrorCode::VENUE_ERROR,
               command.diff.plan_id, {}, "runtime is closed or engine is not active");
    finishCommand();
    co_return;
  }
  for (const auto& operation : command.diff.operations) {
    if (operation.type == domain::OrderOperationType::CANCEL) {
      const auto result = m_venue->cancel(operation.order_id);
      if (!result.accepted) {
        diagnostic(domain::DiagnosticSeverity::ERROR, result.error.code,
                   command.diff.plan_id, operation.order_id, result.error.message);
        VLOG(1) << "运行时撤单提交失败: " << result.error.message;
      }
      continue;
    }
    if (!operation.intent) continue;
    const std::string id = orderId(*operation.intent, command.diff.plan_id);
    if (operation.intent->type == domain::OrderType::LIMIT) {
      const auto reserve = m_ledger->reserve(*operation.intent, operation.intent->quantity);
      if (!reserve.accepted) {
        diagnostic(domain::DiagnosticSeverity::WARNING, reserve.error.code,
                   command.diff.plan_id, id, reserve.error.message);
        VLOG(1) << "运行时订单冻结失败: " << reserve.error.message;
        continue;
      }
    }
    const auto registered = m_orders.registerOrder(id, command.diff.strategy_id, *operation.intent);
    if (!registered.accepted) {
      if (operation.intent->type == domain::OrderType::LIMIT) {
        m_ledger->release(*operation.intent, operation.intent->quantity);
      }
      diagnostic(domain::DiagnosticSeverity::ERROR, registered.error.code,
                 command.diff.plan_id, id, registered.error.message);
      VLOG(1) << "运行时订单注册失败: " << registered.error.message;
      continue;
    }
    const auto result = m_venue->submit(id, *operation.intent);
    if (!result.accepted) {
      m_orders.apply({domain::ExecutionEventType::REJECTED, id + ":submit", id,
                      operation.intent->intent_id, operation.intent->symbol,
                      operation.intent->side, operation.intent->type,
                      operation.intent->price, operation.intent->quantity, 0, 0, 0, {}});
      if (operation.intent->type == domain::OrderType::LIMIT) {
        m_ledger->release(*operation.intent, operation.intent->quantity);
      }
      diagnostic(domain::DiagnosticSeverity::ERROR, result.error.code,
                 command.diff.plan_id, id, result.error.message);
      VLOG(1) << "运行时下单提交失败: " << result.error.message;
    }
  }
  finishCommand();
  co_return;
}

void StrategyRuntime::finishCommand() {
  std::lock_guard<std::mutex> lock(m_drain->mutex);
  if (m_drain->active > 0) --m_drain->active;
  m_drain->cv.notify_all();
}

void StrategyRuntime::onExecution(const domain::ExecutionReport& report) {
  const auto active = m_orders.find(report.order_id);
  if (!active || !m_ledger) return;

  const dec_float previous_filled = active->filled_quantity;
  m_orders.apply(report);
  if (report.type == domain::ExecutionEventType::CANCELLED ||
      report.type == domain::ExecutionEventType::REJECTED) {
    const dec_float remaining = active->intent.quantity - previous_filled;
    if (remaining > 0 && active->intent.type == domain::OrderType::LIMIT) {
      m_ledger->release(active->intent, remaining);
    }
    if (m_execution_handler) m_execution_handler(report);
    return;
  }
  if (report.type != domain::ExecutionEventType::FILL &&
      report.type != domain::ExecutionEventType::PARTIAL_FILL) {
    return;
  }

  dec_float cumulative = report.filled_quantity;
  if (cumulative <= 0) cumulative = report.quantity;
  const dec_float delta = cumulative > previous_filled
      ? cumulative - previous_filled : dec_float(0);
  if (delta <= 0) return;

  auto normalized = report;
  normalized.quantity = delta;
  normalized.filled_quantity = delta;
  normalized.execution_id = report.execution_id + ":delta:" + delta.str();
  m_ledger->apply(normalized);
  if (active->intent.type == domain::OrderType::LIMIT) {
    m_ledger->release(active->intent, delta);
  }
  if (m_execution_handler) m_execution_handler(normalized);
}

void StrategyRuntime::setMarketFeed(std::shared_ptr<market::MarketDataFeed> feed) {
  m_feed = std::move(feed);
  if (!m_feed) return;
  // 行情快照由数据源单向写入策略上下文，策略侧只读取。
  m_feed->setCallback([context = m_context](const domain::MarketSnapshot& snapshot) {
    context->updateMarket(snapshot);
  });
}

domain::CommandResult StrategyRuntime::subscribeMarket(
    const domain::MarketSubscription& subscription) {
  if (!m_feed) {
    return {false,
            {domain::ErrorCode::UNSUPPORTED, "market data feed is not configured"},
            subscription.symbol};
  }
  return m_feed->subscribe(subscription);
}

void StrategyRuntime::diagnostic(domain::DiagnosticSeverity severity,
                                 domain::ErrorCode code,
                                 const std::string& plan_id,
                                 const std::string& order_id,
                                 const std::string& message) {
  if (!m_diagnostic_callback) return;
  m_diagnostic_callback({severity, code, plan_id, order_id, message});
}

std::string StrategyRuntime::planScope(const domain::OrderPlanDiff& diff) {
  return diff.strategy_id + "|" + diff.symbol;
}

std::string StrategyRuntime::orderId(const domain::OrderIntent& intent,
                                     const std::string& plan_id) const {
  if (!intent.intent_id.empty()) return fmt::format("{}:{}", plan_id, intent.intent_id);
  return fmt::format("{}:{}:{}:{}", plan_id, intent.symbol,
                     static_cast<int>(intent.side), intent.level);
}

}  // namespace core::runtime
