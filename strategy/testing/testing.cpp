#include "testing.h"

#include <boost/asio/steady_timer.hpp>
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

  // boost::asio::steady_timer timer(executor);
  // timer.expires_after(1s);
  // co_await timer.async_wait(asio::use_awaitable);
  
  const auto context = runtime_context();
  if (!context) {
    LOG(WARNING) << "[测试策略] 未注入策略运行时上下文，无法提交订单计划";
    co_return;
  }

  core::domain::OrderPlan plan;
  plan.plan_id = "testing-market-buy";
  plan.idempotency_key.value = plan.plan_id;
  plan.strategy_id = "testing:BTC-USDT-SWAP";
  plan.symbol = "BTC-USDT-SWAP";
  plan.replace_policy = core::domain::ReplacePolicy::KEEP_EXISTING;
  core::domain::OrderIntent intent;
  intent.intent_id = "market-buy";
  intent.symbol = plan.symbol;
  intent.side = core::domain::Side::BUY;
  intent.type = core::domain::OrderType::MARKET;
  intent.quantity = dec_float("0.01");
  plan.intents.push_back(std::move(intent));

  const auto result = co_await context->submitAsync(plan);
  if (!result.accepted) {
    LOG(WARNING) << fmt::format("[测试策略] 市价单未提交: {}", result.error.message);
  } else {
    LOG(INFO) << "[测试策略] 已通过通用运行时提交市价买单计划";
  }
  co_return;
}

asio::awaitable<void> Testing::recv_account(engine::AccountDataPtr account) {
  if (!account) co_return;
  // Runtime 模式下账户状态以统一账本快照为准，Legacy 事件由 LegacyLedgerAdapter 单向同步。
  LOG(INFO) << fmt::format("recv_account: {}", account->balance.str());
  co_return;
}

// 接收持仓数据并打印详细信息
asio::awaitable<void> Testing::recv_position(engine::PositionDataPtr position) {
  if (!position) co_return;
  LOG(INFO) << fmt::format("recv_position: {}", position->items.size());
  // 遍历所有持仓，打印交易对、数量、价格和方向；Runtime 模式下策略只读不写账本。
  for (auto& item : position->items) {
    if (!item) continue;
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

asio::awaitable<void> Testing::recv_bar(engine::BarDataPtr bar) {
  LOG(INFO) << fmt::format("recv_bar: {} close={}", bar->symbol, bar->close_price.str());
  co_return;
}

asio::awaitable<void> Testing::recv_order(engine::OrderDataPtr order) {
  LOG(INFO) << fmt::format("recv_order: {}", order->items.size());
  co_return;
}

}
