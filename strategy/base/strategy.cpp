#include "strategy.h"
#include <glog/logging.h>

#include <utility>

#include "core/runtime/strategy_runtime.h"

namespace strategy::base {

Strategy::Strategy(engine::EnginePtr engine) : m_engine(engine) {
}

Strategy::~Strategy() {}

void Strategy::set_runtime_context(
    std::shared_ptr<core::runtime::StrategyContext> context,
    std::shared_ptr<core::runtime::StrategyRuntime> runtime) {
  m_runtime_context = std::move(context);
  m_runtime = std::move(runtime);
}

// 初始化策略，注册各类事件的回调函数
asio::awaitable<void> Strategy::init() {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(ERROR) << "Engine has been destroyed, cannot initialize strategy";
    co_return;
  }

  // 注册账户数据事件回调
  engine->register_callback<engine::AccountData>(engine::EventType::kAccount,
    std::bind(&Strategy::recv_account, shared_from_this(), std::placeholders::_1));

  // 注册持仓数据事件回调
  engine->register_callback<engine::PositionData>(engine::EventType::kPosition,
    std::bind(&Strategy::recv_position, shared_from_this(), std::placeholders::_1));

  // 注册订单簿数据事件回调
  engine->register_callback<engine::Book>(engine::EventType::kBook,
    std::bind(&Strategy::recv_book, shared_from_this(), std::placeholders::_1));

  // 注册Tick数据事件回调
  engine->register_callback<engine::TickData>(engine::EventType::kTick,
    std::bind(&Strategy::recv_tick, shared_from_this(), std::placeholders::_1));

  // 注册 K 线数据事件回调
  engine->register_callback<engine::BarData>(engine::EventType::kBar,
    std::bind(&Strategy::recv_bar, shared_from_this(), std::placeholders::_1));

  // 注册订单数据事件回调
  engine->register_callback<engine::OrderData>(engine::EventType::kOrder,
    std::bind(&Strategy::recv_order, shared_from_this(), std::placeholders::_1));
  
  co_return;
}

asio::awaitable<void> Strategy::on_message(engine::MessageDataPtr msg) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send message event";
    co_return;
  }
  co_await engine->on_event(engine::EventType::kMessage, msg);
}

asio::awaitable<void> Strategy::on_request_account() {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot request account";
    co_return;
  }
  co_await engine->on_event(engine::EventType::kQueryAccount, std::make_shared<engine::QueryAccountData>());
}

asio::awaitable<void> Strategy::on_request_position() {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot request position";
    co_return;
  }
  co_await engine->on_event(engine::EventType::kQueryPosition, std::make_shared<engine::QueryPositionData>());
}

// 订阅指定交易对的订单簿数据
asio::awaitable<void> Strategy::on_subscribe_book(const std::string& symbol) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot subscribe book for " << symbol;
    co_return;
  }
  auto book = std::make_shared<engine::SubscribeData>();
  book->symbol = symbol;
  co_await engine->on_event(engine::EventType::kSubscribeBook, book);
}

// 订阅指定交易对的Tick数据
asio::awaitable<void> Strategy::on_subscribe_tick(const std::string& symbol) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot subscribe tick for " << symbol;
    co_return;
  }
  auto tick = std::make_shared<engine::SubscribeData>();
  tick->symbol = symbol;
  co_await engine->on_event(engine::EventType::kSubscribeTick, tick);
}

}  // namespace strategy::base
