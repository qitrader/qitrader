#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "backtest/match/match_engine.h"
#include "engine/object.h"

namespace {

const char* kSymbol = "ETH-USDT";

std::shared_ptr<engine::OrderData> makeLimitOrder(const std::string& id,
                                                  engine::Direction direction,
                                                  const dec_float& price,
                                                  const dec_float& volume) {
  auto item = std::make_shared<engine::OrderDataItem>();
  item->order_id = id;
  item->symbol = kSymbol;
  item->direction = direction;
  item->price = price;
  item->volume = volume;
  item->otype = engine::OrderType::LIMIT;
  item->status = engine::OrderStatus::SUBMITTING;
  auto order = std::make_shared<engine::OrderData>();
  order->symbol = kSymbol;
  order->timestamp_ms = 1000;
  order->items.push_back(item);
  return order;
}

std::shared_ptr<engine::TickData> makeTick(int64_t timestamp_ms, const dec_float& price,
                                           const dec_float& volume,
                                           const std::shared_ptr<engine::Book>& book) {
  auto tick = std::make_shared<engine::TickData>();
  tick->symbol = kSymbol;
  tick->timestamp_ms = timestamp_ms;
  tick->last_price = price;
  tick->last_volume = volume;
  tick->order_book = book;
  return tick;
}

std::shared_ptr<engine::Book> makeBook(const dec_float& bid_price, const dec_float& bid_volume,
                                       const dec_float& ask_price, const dec_float& ask_volume) {
  auto book = std::make_shared<engine::Book>();
  book->symbol = kSymbol;
  engine::BookItem bid;
  bid.price = bid_price;
  bid.volume = bid_volume;
  book->bids.push_back(bid);
  engine::BookItem ask;
  ask.price = ask_price;
  ask.volume = ask_volume;
  book->asks.push_back(ask);
  return book;
}

void testQueueModelDelaysFill() {
  // 价格穿越只是"轮到的必要条件"，前面的排队量要被主动成交量吃掉才成交。
  backtest::match::MatchEngine match;
  const auto book = makeBook(dec_float("100"), dec_float("3"), dec_float("102"), dec_float("3"));
  // 先送一条不穿越的行情，让引擎拿到盘口
  match.onTick(makeTick(1000, dec_float("101"), dec_float("2"), book));
  match.submitOrder(makeLimitOrder("bid-1", engine::Direction::BUY, dec_float("100"),
                                   dec_float("5")),
                    dec_float("101"));
  assert(match.trades().empty());

  // 队列 3，每笔 tick 动用 2 * 0.3 = 0.6，前 4 笔累计 2.4 仍不足以排到
  for (int i = 0; i < 4; ++i) {
    match.onTick(makeTick(2000 + i, dec_float("100"), dec_float("2"), book));
    assert(match.trades().empty());
  }
  match.onTick(makeTick(2100, dec_float("100"), dec_float("2"), book));
  assert(match.trades().size() == 1);
  assert(match.trades().front()->volume == dec_float("5"));
  assert(match.trades().front()->price == dec_float("100"));
}

void testQueueModelDisabledFillsImmediately() {
  backtest::match::MatchEngine match;
  backtest::match::FillModelConfig config;
  config.queue_model = false;
  match.setFillModel(config);

  match.submitOrder(makeLimitOrder("bid-1", engine::Direction::BUY, dec_float("100"),
                                   dec_float("5")),
                    dec_float("101"));
  match.onTick(makeTick(2000, dec_float("100"), dec_float("2"), nullptr));
  assert(match.trades().size() == 1);
}

void testQueueAdvanceIsIdempotentPerTick() {
  // 回测一帧会调两次 onTick，队列推进必须按行情时间戳去重。
  backtest::match::MatchEngine match;
  const auto book = makeBook(dec_float("100"), dec_float("3"), dec_float("102"), dec_float("3"));
  match.onTick(makeTick(1000, dec_float("101"), dec_float("2"), book));
  match.submitOrder(makeLimitOrder("bid-1", engine::Direction::BUY, dec_float("100"),
                                   dec_float("5")),
                    dec_float("101"));

  // 同一时间戳重复 10 次，只应推进一次队列（0.6）
  for (int i = 0; i < 10; ++i) {
    match.onTick(makeTick(2000, dec_float("100"), dec_float("2"), book));
  }
  assert(match.trades().empty());

  // 之后按新时间戳推进，累计到 3.0 时成交
  for (int i = 0; i < 4; ++i) {
    match.onTick(makeTick(3000 + i, dec_float("100"), dec_float("2"), book));
  }
  assert(match.trades().size() == 1);
}

void testAdverseSelectionSlippage() {
  backtest::match::MatchEngine match;
  backtest::match::FillModelConfig config;
  config.queue_model = false;
  config.adverse_slippage_bps = 10;  // 0.1%
  match.setFillModel(config);

  // 买单成交在更低的价格、卖单成交在更高的价格：成交总是发生在不利方向
  match.submitOrder(makeLimitOrder("bid-1", engine::Direction::BUY, dec_float("100"),
                                   dec_float("1")),
                    dec_float("101"));
  match.submitOrder(makeLimitOrder("ask-1", engine::Direction::SELL, dec_float("100"),
                                   dec_float("1")),
                    dec_float("99"));
  match.onTick(makeTick(2000, dec_float("100"), dec_float("2"), nullptr));
  assert(match.trades().size() == 2);
  for (const auto& trade : match.trades()) {
    if (trade->direction == engine::Direction::BUY) {
      assert(trade->price == dec_float("99.9"));
    } else {
      assert(trade->price == dec_float("100.1"));
    }
  }
}

void testFallbackWithoutOrderBook() {
  // 回测 CSV 没有盘口：退化成"半个挂单量"，仍能成交，不会永久挂零。
  backtest::match::MatchEngine match;
  match.submitOrder(makeLimitOrder("bid-1", engine::Direction::BUY, dec_float("100"),
                                   dec_float("1")),
                    dec_float("101"));
  // 无成交量时按 default_trade_volume=1 折算：每 tick 0.3，队列 0.5
  match.onTick(makeTick(2000, dec_float("100"), dec_float("0"), nullptr));
  assert(match.trades().empty());
  match.onTick(makeTick(3000, dec_float("100"), dec_float("0"), nullptr));
  assert(match.trades().size() == 1);
}

}  // namespace

void runMatchEngineTests() {
  testQueueModelDelaysFill();
  testQueueModelDisabledFillsImmediately();
  testQueueAdvanceIsIdempotentPerTick();
  testAdverseSelectionSlippage();
  testFallbackWithoutOrderBook();
}
