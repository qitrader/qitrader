#ifndef QITRADER_CORE_MARKET_MARKET_DATA_FEED_H_
#define QITRADER_CORE_MARKET_MARKET_DATA_FEED_H_

#include <functional>

#include "core/domain/types.h"

namespace core::market {

struct MarketFeedCapabilities {
  bool tick{false};
  bool book{false};
  bool bar{false};
  bool historical{false};
};

using MarketEventCallback = std::function<void(const domain::MarketSnapshot&)>;

/**
 * @brief 统一行情数据端口，隔离交易所、CSV 和 Paper 行情来源。
 */
class MarketDataFeed {
 public:
  virtual ~MarketDataFeed() = default;
  virtual domain::CommandResult subscribe(const domain::MarketSubscription& subscription) = 0;
  virtual asio::awaitable<void> run() { co_return; }
  virtual void setCallback(MarketEventCallback callback) = 0;
  virtual MarketFeedCapabilities capabilities() const = 0;
};

}  // namespace core::market

#endif  // QITRADER_CORE_MARKET_MARKET_DATA_FEED_H_
