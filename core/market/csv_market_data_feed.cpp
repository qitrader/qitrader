#include "csv_market_data_feed.h"

#include <utility>

namespace core::market {

CsvMarketDataFeed::CsvMarketDataFeed(std::string file_path,
                                     std::string start_date,
                                     std::string end_date)
    : m_loader(std::move(file_path), std::move(start_date), std::move(end_date)) {}

domain::CommandResult CsvMarketDataFeed::subscribe(
    const domain::MarketSubscription& subscription) {
  if (subscription.symbol.empty() ||
      (!subscription.tick && !subscription.book && !subscription.bar)) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid CSV subscription"}, {}};
  }
  m_symbols.insert(subscription.symbol);
  return {true, {}, subscription.symbol};
}

asio::awaitable<void> CsvMarketDataFeed::run() {
  if (!m_callback) co_return;

  const auto ticks = m_loader.loadTicks();
  if (!ticks.empty()) {
    for (const auto& tick : ticks) {
      if (!tick || !subscribed(tick->symbol)) continue;
      m_callback(fromTick(*tick));
      co_await asio::post(asio::use_awaitable);
    }
    co_return;
  }

  const auto bars = m_loader.loadBars();
  for (const auto& bar : bars) {
    if (!bar || !subscribed(bar->symbol)) continue;
    m_callback(fromBar(*bar));
    co_await asio::post(asio::use_awaitable);
  }
}

void CsvMarketDataFeed::setCallback(MarketEventCallback callback) {
  m_callback = std::move(callback);
}

void CsvMarketDataFeed::pushTick(const engine::TickData& tick) {
  if (!subscribed(tick.symbol)) return;
  emit(fromTick(tick));
}

void CsvMarketDataFeed::pushBar(const engine::BarData& bar) {
  if (!subscribed(bar.symbol)) return;
  emit(fromBar(bar));
}

void CsvMarketDataFeed::emit(const domain::MarketSnapshot& snapshot) {
  if (m_callback) m_callback(snapshot);
}

MarketFeedCapabilities CsvMarketDataFeed::capabilities() const {
  return {.tick = true, .book = false, .bar = true, .historical = true};
}

bool CsvMarketDataFeed::isValid() const {
  return m_loader.isValid();
}

bool CsvMarketDataFeed::subscribed(const std::string& symbol) const {
  return m_symbols.empty() || m_symbols.contains(symbol);
}

domain::MarketSnapshot CsvMarketDataFeed::fromTick(const engine::TickData& tick) {
  domain::MarketSnapshot snapshot;
  snapshot.symbol = tick.symbol;
  snapshot.exchange = tick.exchange;
  snapshot.timestamp_ms = tick.timestamp_ms;
  snapshot.last_price = tick.last_price;
  snapshot.last_quantity = tick.last_volume;
  if (tick.order_book) {
    for (const auto& level : tick.order_book->bids) {
      snapshot.bids.push_back({level.price, level.volume});
    }
    for (const auto& level : tick.order_book->asks) {
      snapshot.asks.push_back({level.price, level.volume});
    }
    if (!snapshot.bids.empty()) snapshot.bid_price = snapshot.bids.front().price;
    if (!snapshot.asks.empty()) snapshot.ask_price = snapshot.asks.front().price;
  }
  return snapshot;
}

domain::MarketSnapshot CsvMarketDataFeed::fromBar(const engine::BarData& bar) {
  domain::MarketSnapshot snapshot;
  snapshot.symbol = bar.symbol;
  snapshot.exchange = bar.exchange;
  snapshot.timestamp_ms = bar.timestamp_ms;
  snapshot.last_price = bar.close_price;
  snapshot.last_quantity = bar.volume;
  return snapshot;
}

}  // namespace core::market
