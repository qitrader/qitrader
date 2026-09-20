#ifndef QITRADER_CORE_MARKET_GATEWAY_MARKET_DATA_ADAPTER_H_
#define QITRADER_CORE_MARKET_GATEWAY_MARKET_DATA_ADAPTER_H_

#include <memory>

#include "core/market/market_data_feed.h"
#include "engine/engine.h"

namespace core::market {

/**
 * @brief 将旧 Engine 的 Tick/Book 事件适配为标准化 MarketSnapshot。
 */
class GatewayMarketDataAdapter final : public MarketDataFeed,
                                       public std::enable_shared_from_this<GatewayMarketDataAdapter> {
 public:
  explicit GatewayMarketDataAdapter(engine::EnginePtr engine);

  domain::CommandResult subscribe(const domain::MarketSubscription& subscription) override;
  asio::awaitable<void> run() override { co_return; }
  void setCallback(MarketEventCallback callback) override;
  MarketFeedCapabilities capabilities() const override;

 private:
  asio::awaitable<void> dispatchSubscription(domain::MarketSubscription subscription);
  asio::awaitable<void> onTick(engine::TickDataPtr tick);
  asio::awaitable<void> onBook(engine::BookPtr book);
  asio::awaitable<void> onBar(engine::BarDataPtr bar);
  static domain::MarketSnapshot fromTick(const engine::TickData& tick);
  static domain::MarketSnapshot fromBar(const engine::BarData& bar);
  static void mergeBook(domain::MarketSnapshot& snapshot, const engine::Book& book);

  engine::EnginePtr m_engine;
  MarketEventCallback m_callback;
};

}  // namespace core::market

#endif  // QITRADER_CORE_MARKET_GATEWAY_MARKET_DATA_ADAPTER_H_
