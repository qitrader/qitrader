#include "testing.h"

#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include "utils/utils.h"

using namespace std::chrono_literals;

namespace strategy::testing {

Testing::Testing(engine::EnginePtr engine) : base::Strategy(engine) {}

Testing::~Testing() {}

// 策略启动后执行的主逻辑
asio::awaitable<void> Testing::run() {
  auto executor = co_await asio::this_coro::executor;
  LOG(INFO) << fmt::format("run");

  // 查询账户信息
  co_await on_request_account();
  
  // 查询持仓信息
  co_await on_request_position();
  
  // // 订阅BTC-USDT-SWAP的订单簿数据
  // co_await on_subscribe_book("BTC-USDT-SWAP");
  
  // // 订阅BTC-USDT-SWAP的Tick数据
  // co_await on_subscribe_tick("BTC-USDT-SWAP");

  boost::asio::steady_timer timer(executor);
  timer.expires_after(1s);
  co_await timer.async_wait(asio::use_awaitable);
  
  auto order = std::make_shared<engine::OrderData>();
  auto order_item = std::make_shared<engine::OrderDataItem>();
  order_item->symbol = "BTC-USDT-SWAP";
  order_item->direction = engine::Direction::BUY;
  order_item->otype = engine::OrderType::MARKET;
  order_item->volume = dec_float("0.01");
  order->items.push_back(order_item);
  co_await on_send_order(order);
  co_return;
}

asio::awaitable<void> Testing::recv_account(engine::AccountDataPtr account) {
  LOG(INFO) << fmt::format("recv_account: {}", account->balance.str());
  co_return;
}

// 接收持仓数据并打印详细信息
asio::awaitable<void> Testing::recv_position(engine::PositionDataPtr position) {
  LOG(INFO) << fmt::format("recv_position: {}", position->items.size());
  // 遍历所有持仓，打印交易对、数量、价格和方向
  for (auto& item : position->items) {
    LOG(INFO) << fmt::format("position: {}, {} {} {}", item->symbol, item->volume.str(), item->price.str(), int(item->direction));
  }
  co_return;
}

// 接收订单簿数据并打印买卖盘信息
asio::awaitable<void> Testing::recv_book(engine::BookPtr order) {
  LOG(INFO) << fmt::format("recv_book {}: ask {} bid {}", order->symbol, order->asks.size(), order->bids.size());

  co_return;
}

asio::awaitable<void> Testing::recv_tick(engine::TickDataPtr ticker) {
  LOG(INFO) << fmt::format("recv_tick: {}", ticker->symbol);
  co_return;
}

asio::awaitable<void> Testing::recv_order(engine::OrderDataPtr order) {
  LOG(INFO) << fmt::format("recv_order: {}", order->items.size());
  co_return;
}

}
