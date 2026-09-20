#ifndef QITRADER_CORE_MARKET_CSV_MARKET_DATA_FEED_H_
#define QITRADER_CORE_MARKET_CSV_MARKET_DATA_FEED_H_

#include <set>
#include <string>

#include "backtest/data/csv_loader.h"
#include "market_data_feed.h"

namespace core::market {

/**
 * @brief 基于 CsvLoader 的历史行情 MarketDataFeed。
 *
 * 支持两种驱动方式：
 * - `run()` 自行回放整份文件，适合离线训练和强化学习环境；
 * - `pushTick()` / `pushBar()` 由外部回放循环逐条喂入，适合已经由
 *   `BacktestGateway` 统一控制时间线的回测场景，避免重复回放。
 */
class CsvMarketDataFeed final : public MarketDataFeed {
 public:
  CsvMarketDataFeed(std::string file_path, std::string start_date = {},
                    std::string end_date = {});

  domain::CommandResult subscribe(const domain::MarketSubscription& subscription) override;
  asio::awaitable<void> run() override;
  void setCallback(MarketEventCallback callback) override;
  MarketFeedCapabilities capabilities() const override;

  bool isValid() const;

  /// 由外部回放循环喂入一条 Tick 并转换为标准化快照。
  void pushTick(const engine::TickData& tick);

  /// 由外部回放循环喂入一条 K 线并转换为标准化快照。
  void pushBar(const engine::BarData& bar);

 private:
  void emit(const domain::MarketSnapshot& snapshot);
  bool subscribed(const std::string& symbol) const;
  static domain::MarketSnapshot fromTick(const engine::TickData& tick);
  static domain::MarketSnapshot fromBar(const engine::BarData& bar);

  backtest::data::CsvLoader m_loader;
  std::set<std::string> m_symbols;
  MarketEventCallback m_callback;
};

}  // namespace core::market

#endif  // QITRADER_CORE_MARKET_CSV_MARKET_DATA_FEED_H_
