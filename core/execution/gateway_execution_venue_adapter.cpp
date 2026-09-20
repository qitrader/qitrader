#include "gateway_execution_venue_adapter.h"

#include <boost/asio/detached.hpp>
#include <fmt/format.h>
#include <glog/logging.h>

#include <chrono>

namespace core::execution {

namespace {
engine::Direction toEngineDirection(domain::Side side) {
  return side == domain::Side::BUY ? engine::Direction::BUY : engine::Direction::SELL;
}

engine::OrderType toEngineOrderType(domain::OrderType type) {
  return type == domain::OrderType::MARKET ? engine::OrderType::MARKET : engine::OrderType::LIMIT;
}
}

GatewayExecutionVenueAdapter::GatewayExecutionVenueAdapter(engine::EnginePtr engine)
    : m_engine(std::move(engine)) {
  if (m_engine) {
    m_engine->register_callback<engine::OrderData>(
        engine::EventType::kOrder,
        [this](engine::OrderDataPtr order) { return onOrder(std::move(order)); });
  }
}

domain::CommandResult GatewayExecutionVenueAdapter::submit(
    const std::string& order_id, const domain::OrderIntent& intent) {
  if (!m_engine || order_id.empty() || intent.symbol.empty() || intent.quantity <= 0) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid gateway order command"}, order_id};
  }
  // 引擎未运行或已停止时不再投递事件，避免协程永久挂起在事件通道上。
  if (m_closed.load() || !m_engine->is_running() || m_engine->is_stopping()) {
    return {false, {domain::ErrorCode::VENUE_ERROR, "engine is not active"}, order_id};
  }
  if (m_intents.contains(order_id)) {
    return {false, {domain::ErrorCode::DUPLICATE, "order command already exists"}, order_id};
  }
  m_intents.emplace(order_id, intent);
  auto self = shared_from_this();
  beginDispatch();
  asio::co_spawn(m_engine->executor(),
      [self, order_id, intent]() -> asio::awaitable<void> {
        co_await self->dispatchOrderGuarded(order_id, intent);
      }, asio::detached);
  return {true, {}, order_id};
}

asio::awaitable<void> GatewayExecutionVenueAdapter::dispatchOrderGuarded(
    const std::string& order_id, const domain::OrderIntent& intent) {
  co_await dispatchOrder(order_id, intent);
  endDispatch();
}

asio::awaitable<void> GatewayExecutionVenueAdapter::dispatchCancelGuarded(
    const std::string& order_id) {
  co_await dispatchCancel(order_id);
  endDispatch();
}

void GatewayExecutionVenueAdapter::beginDispatch() {
  std::lock_guard<std::mutex> lock(m_dispatch_mutex);
  ++m_in_flight;
}

void GatewayExecutionVenueAdapter::endDispatch() {
  std::lock_guard<std::mutex> lock(m_dispatch_mutex);
  if (m_in_flight > 0) --m_in_flight;
  m_dispatch_cv.notify_all();
}

bool GatewayExecutionVenueAdapter::waitIdle(uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(m_dispatch_mutex);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (m_in_flight > 0) {
    if (m_dispatch_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
      return m_in_flight == 0;
    }
  }
  return true;
}

domain::CommandResult GatewayExecutionVenueAdapter::cancel(const std::string& order_id) {
  if (!m_engine || order_id.empty()) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid cancel command"}, order_id};
  }
  if (m_closed.load() || !m_engine->is_running() || m_engine->is_stopping()) {
    return {false, {domain::ErrorCode::VENUE_ERROR, "engine is not active"}, order_id};
  }
  auto self = shared_from_this();
  beginDispatch();
  asio::co_spawn(m_engine->executor(),
      [self, order_id]() -> asio::awaitable<void> {
        co_await self->dispatchCancelGuarded(order_id);
      }, asio::detached);
  return {true, {}, order_id};
}

VenueCapabilities GatewayExecutionVenueAdapter::capabilities() const {
  return {.supports_cancel = true, .supports_replace = false,
          .supports_batch_orders = false, .supports_partial_fill = true};
}

void GatewayExecutionVenueAdapter::setCallback(ExecutionCallback callback) {
  m_callback = std::move(callback);
}

asio::awaitable<void> GatewayExecutionVenueAdapter::dispatchOrder(
    const std::string& order_id, const domain::OrderIntent& intent) {
  // 引擎停止或执行端口关闭后不再向事件通道投递命令，避免停止阶段事件悬挂。
  if (!m_engine || m_closed.load() || !m_engine->is_running() ||
      m_engine->is_stopping()) {
    co_return;
  }
  if (!m_engine->is_running() || m_engine->is_stopping()) co_return;
  auto order = std::make_shared<engine::OrderData>();
  order->symbol = intent.symbol;
  auto item = std::make_shared<engine::OrderDataItem>();
  item->order_id = order_id;
  item->symbol = intent.symbol;
  item->direction = toEngineDirection(intent.side);
  item->otype = toEngineOrderType(intent.type);
  item->price = intent.price;
  item->volume = intent.quantity;
  item->status = engine::OrderStatus::SUBMITTING;
  // 透传只减仓约束，否则策略声明的 reduce_only 会在适配层被静默丢弃。
  item->reduce_only = intent.reduce_only;
  order->items.push_back(item);
  co_await m_engine->on_event(engine::EventType::kSendOrder, order);
}

asio::awaitable<void> GatewayExecutionVenueAdapter::dispatchCancel(
    const std::string& order_id) {
  if (!m_engine || m_closed.load() || !m_engine->is_running() ||
      m_engine->is_stopping()) {
    co_return;
  }
  if (!m_engine->is_running() || m_engine->is_stopping()) co_return;
  auto request = std::make_shared<engine::OrderData>();
  auto item = std::make_shared<engine::OrderDataItem>();
  item->order_id = order_id;
  item->status = engine::OrderStatus::PENDING;
  request->items.push_back(item);
  co_await m_engine->on_event(engine::EventType::kCancelOrder, request);
}

asio::awaitable<void> GatewayExecutionVenueAdapter::onOrder(engine::OrderDataPtr order) {
  if (!order || !m_callback) co_return;
  for (const auto& item : order->items) {
    if (!item || item->order_id.empty()) continue;
    const auto intent_it = m_intents.find(item->order_id);
    domain::ExecutionReport report;
    report.execution_id = fmt::format("order:{}:{}:{}", item->order_id,
                                      static_cast<int>(item->status),
                                      item->filled_volume.str());
    report.order_id = item->order_id;
    report.symbol = item->symbol.empty() ? order->symbol : item->symbol;
    report.side = item->direction == engine::Direction::BUY ? domain::Side::BUY : domain::Side::SELL;
    report.order_type = item->otype == engine::OrderType::MARKET
        ? domain::OrderType::MARKET : domain::OrderType::LIMIT;
    report.price = item->price;
    report.quantity = item->volume;
    report.filled_quantity = item->filled_volume;
    report.type = mapState(item->status);
    if (intent_it != m_intents.end()) report.intent_id = intent_it->second.intent_id;
    m_callback(report);
    if (report.type == domain::ExecutionEventType::FILL ||
        report.type == domain::ExecutionEventType::CANCELLED ||
        report.type == domain::ExecutionEventType::REJECTED) {
      m_intents.erase(item->order_id);
    }
  }
  co_return;
}

domain::ExecutionEventType GatewayExecutionVenueAdapter::mapState(engine::OrderStatus status) {
  switch (status) {
    case engine::OrderStatus::SUBMITTING: return domain::ExecutionEventType::ACCEPTED;
    case engine::OrderStatus::PENDING: return domain::ExecutionEventType::PENDING;
    case engine::OrderStatus::PARTIAL_FILLED: return domain::ExecutionEventType::PARTIAL_FILL;
    case engine::OrderStatus::FILLED: return domain::ExecutionEventType::FILL;
    case engine::OrderStatus::CANCELLED: return domain::ExecutionEventType::CANCELLED;
    case engine::OrderStatus::REJECTED: return domain::ExecutionEventType::REJECTED;
  }
  return domain::ExecutionEventType::REJECTED;
}

}  // namespace core::execution
