#include "gateway.h"
#include <glog/logging.h>

namespace market::base {

Gateway::Gateway(EnginePtr engine, const std::string& name) : m_engine(engine), m_name(name) {}

Gateway::~Gateway() {}

asio::awaitable<void> Gateway::stop_engine() {
  auto engine = m_engine.lock();
  if (engine) {
    co_await engine->stop();
  }
}

// 初始化网关，注册各类查询和订阅请求的回调函数
asio::awaitable<void> Gateway::init() {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(ERROR) << "Engine has been destroyed, cannot initialize gateway: " << m_name;
    co_return;
  }

  // 注册查询账户请求的回调
  engine->register_callback<engine::QueryAccountData>(engine::EventType::kQueryAccount,
    std::bind(&Gateway::query_account, shared_from_this(), std::placeholders::_1));
  
  // 注册查询持仓请求的回调
  engine->register_callback<engine::QueryPositionData>(engine::EventType::kQueryPosition,
    std::bind(&Gateway::query_position, shared_from_this(), std::placeholders::_1));
  
  // 注册查询订单请求的回调
  engine->register_callback<engine::QueryOrderData>(engine::EventType::kQueryOrder,
    std::bind(&Gateway::query_order, shared_from_this(), std::placeholders::_1));

  // 注册订阅订单簿请求的回调
  engine->register_callback<engine::SubscribeData>(engine::EventType::kSubscribeBook,
    std::bind(&Gateway::subscribe_book, shared_from_this(), std::placeholders::_1));
  
  // 注册订阅Tick请求的回调
  engine->register_callback<engine::SubscribeData>(engine::EventType::kSubscribeTick,
    std::bind(&Gateway::subscribe_tick, shared_from_this(), std::placeholders::_1));

  // 注册发送订单请求的回调
  engine->register_callback<engine::OrderData>(engine::EventType::kSendOrder,
    std::bind(&Gateway::send_orders, shared_from_this(), std::placeholders::_1));

  // 注册取消订单请求的回调
  engine->register_callback<engine::OrderData>(engine::EventType::kCancelOrder,
    std::bind(&Gateway::cancel_order, shared_from_this(), std::placeholders::_1));
  
  // 调用子类实现的初始化逻辑（如连接WebSocket）
  co_await market_init();
  co_return;
}

asio::awaitable<void> Gateway::on_tick(TickDataPtr tick) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send tick event";
    co_return;
  }
  co_await engine->on_event(EventType::kTick, tick);
}

asio::awaitable<void> Gateway::on_tick_sync(TickDataPtr tick) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send synchronous tick event";
    co_return;
  }
  co_await engine->on_event_sync(EventType::kTick, tick);
}

asio::awaitable<void> Gateway::on_bar(BarDataPtr bar) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send bar event";
    co_return;
  }
  co_await engine->on_event(EventType::kBar, bar);
}

asio::awaitable<void> Gateway::on_bar_sync(BarDataPtr bar) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send synchronous bar event";
    co_return;
  }
  co_await engine->on_event_sync(EventType::kBar, bar);
}

asio::awaitable<void> Gateway::on_position(PositionDataPtr position) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send position event";
    co_return;
  }
  co_await engine->on_event(EventType::kPosition, position);
}

asio::awaitable<void> Gateway::on_account(AccountDataPtr account) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send account event";
    co_return;
  }
  co_await engine->on_event(EventType::kAccount, account);
}

asio::awaitable<void> Gateway::on_book(BookPtr book) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send book event";
    co_return;
  }
  co_await engine->on_event(EventType::kBook, book);
}

asio::awaitable<void> Gateway::on_trade(TradeDataPtr trade) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send trade event";
    co_return;
  }
  co_await engine->on_event(EventType::kTrade, trade);
}

asio::awaitable<void> Gateway::on_order(OrderDataPtr order) {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(WARNING) << "Engine has been destroyed, cannot send order event";
    co_return;
  }
  co_await engine->on_event(EventType::kOrder, order);
}

}
