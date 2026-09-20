#include "gateway_market_data_adapter.h"

#include <boost/asio/detached.hpp>
#include <utility>

namespace core::market {

GatewayMarketDataAdapter::GatewayMarketDataAdapter(engine::EnginePtr engine)
    : m_engine(std::move(engine)) {
  if (!m_engine) return;
  m_engine->register_callback<engine::TickData>(
      engine::EventType::kTick,
      [this](engine::TickDataPtr tick) { return onTick(std::move(tick)); });
  m_engine->register_callback<engine::Book>(
      engine::EventType::kBook,
      [this](engine::BookPtr book) { return onBook(std::move(book)); });
  m_engine->register_callback<engine::BarData>(
      engine::EventType::kBar,
      [this](engine::BarDataPtr bar) { return onBar(std::move(bar)); });
}

domain::CommandResult GatewayMarketDataAdapter::subscribe(
    const domain::MarketSubscription& subscription) {
  if (!m_engine || subscription.symbol.empty() ||
      (!subscription.tick && !subscription.book && !subscription.bar)) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid market subscription"}, {}};
  }
  auto self = shared_from_this();
  asio::co_spawn(m_engine->executor(),
      [self, subscription]() -> asio::awaitable<void> {
        co_await self->dispatchSubscription(subscription);
      }, asio::detached);
  return {true, {}, subscription.symbol};
}

void GatewayMarketDataAdapter::setCallback(MarketEventCallback callback) {
  m_callback = std::move(callback);
}

MarketFeedCapabilities GatewayMarketDataAdapter::capabilities() const {
  return {.tick = true, .book = true, .bar = true, .historical = false};
}

asio::awaitable<void> GatewayMarketDataAdapter::dispatchSubscription(
    domain::MarketSubscription subscription) {
  if (subscription.book) {
    auto request = std::make_shared<engine::SubscribeData>();
    request->symbol = subscription.symbol;
    co_await m_engine->on_event(engine::EventType::kSubscribeBook, request);
  }
  if (subscription.tick) {
    auto request = std::make_shared<engine::SubscribeData>();
    request->symbol = subscription.symbol;
    co_await m_engine->on_event(engine::EventType::kSubscribeTick, request);
  }
}

asio::awaitable<void> GatewayMarketDataAdapter::onTick(engine::TickDataPtr tick) {
  if (!tick || !m_callback) co_return;
  auto snapshot = fromTick(*tick);
  if (tick->order_book) mergeBook(snapshot, *tick->order_book);
  m_callback(snapshot);
}

asio::awaitable<void> GatewayMarketDataAdapter::onBook(engine::BookPtr book) {
  if (!book || !m_callback) co_return;
  domain::MarketSnapshot snapshot;
  snapshot.symbol = book->symbol;
  snapshot.exchange = book->exchange;
  snapshot.timestamp_ms = book->timestamp_ms;
  mergeBook(snapshot, *book);
  if (!snapshot.bids.empty()) snapshot.bid_price = snapshot.bids.front().price;
  if (!snapshot.asks.empty()) snapshot.ask_price = snapshot.asks.front().price;
  // 订单簿消息不带成交价，用最优买卖中价作为参考价；
  // 空盘口推导不出参考价时保留上一次的有效快照，
  // 避免用 last_price 为 0 的无效快照覆盖行情。
  if (snapshot.bid_price && snapshot.ask_price) {
    snapshot.last_price = (*snapshot.bid_price + *snapshot.ask_price) / dec_float(2);
  } else if (snapshot.last_price <= 0) {
    co_return;
  }
  m_callback(snapshot);
}

domain::MarketSnapshot GatewayMarketDataAdapter::fromTick(const engine::TickData& tick) {
  domain::MarketSnapshot snapshot;
  snapshot.symbol = tick.symbol;
  snapshot.exchange = tick.exchange;
  snapshot.timestamp_ms = tick.timestamp_ms;
  snapshot.last_price = tick.last_price;
  snapshot.last_quantity = tick.last_volume;
  return snapshot;
}

domain::MarketSnapshot GatewayMarketDataAdapter::fromBar(const engine::BarData& bar) {
  domain::MarketSnapshot snapshot;
  snapshot.symbol = bar.symbol;
  snapshot.exchange = bar.exchange;
  snapshot.timestamp_ms = bar.timestamp_ms;
  snapshot.last_price = bar.close_price;
  snapshot.last_quantity = bar.volume;
  return snapshot;
}

asio::awaitable<void> GatewayMarketDataAdapter::onBar(engine::BarDataPtr bar) {
  if (!bar || !m_callback) co_return;
  m_callback(fromBar(*bar));
}

void GatewayMarketDataAdapter::mergeBook(domain::MarketSnapshot& snapshot,
                                         const engine::Book& book) {
  snapshot.symbol = snapshot.symbol.empty() ? book.symbol : snapshot.symbol;
  snapshot.exchange = snapshot.exchange.empty() ? book.exchange : snapshot.exchange;
  snapshot.timestamp_ms = std::max(snapshot.timestamp_ms, book.timestamp_ms);
  snapshot.bids.clear();
  snapshot.asks.clear();
  for (const auto& level : book.bids) {
    snapshot.bids.push_back({level.price, level.volume});
  }
  for (const auto& level : book.asks) {
    snapshot.asks.push_back({level.price, level.volume});
  }
}

}  // namespace core::market
