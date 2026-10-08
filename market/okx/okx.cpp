#include "okx.h"

#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <algorithm>

using namespace std::chrono_literals;

namespace market::okx {

Okx::Okx(engine::EnginePtr engine) : base::Gateway(engine, "okx"), http_() {}

// 查询账户信息，通过HTTP API获取并转换为统一格式
asio::awaitable<void> Okx::query_account(engine::QueryAccountDataPtr data) {
  // 调用HTTP API获取账户数据
  auto account = co_await http_.get_account();

  co_await deal_account(account);
  co_return;
}

asio::awaitable<void> Okx::deal_account(const Account& account) {
  // 将OKX格式的账户数据转换为系统统一格式
  auto account_data = std::make_shared<engine::AccountData>();
  account_data->balance = account.totalEq;     // 总资产
  account_data->exchange = name();             // 交易所名称
  account_data->timestamp_ms = account.uTime;  // 更新时间

  // 遍历各币种余额明细
  for (auto& item : account.details) {
    auto balance_item = std::make_shared<engine::BalanceItem>();
    balance_item->symbol = item.ccy;  // 币种
    balance_item->balance = item.eq;  // 余额
    account_data->items.push_back(balance_item);
  }

  // 将账户数据发送到引擎
  co_await on_account(account_data);
}

// 查询持仓信息，通过HTTP API获取并转换为统一格式
asio::awaitable<void> Okx::query_position(engine::QueryPositionDataPtr data) {
  // 调用HTTP API获取持仓数据
  auto positions = co_await http_.get_positions();

  co_await deal_position(positions);
  co_return;
}

asio::awaitable<void> Okx::deal_position(const std::vector<PositionDetail>& positions) {
  auto position_data = std::make_shared<engine::PositionData>();
  position_data->exchange = name();

  // 如果没有持仓，直接返回空数据
  if (positions.empty()) {
    co_await on_position(position_data);
    co_return;
  }

  position_data->timestamp_ms = positions[0].uTime;

  // 遍历所有持仓，转换为统一格式
  for (auto& pos_item : positions) {
    auto item = std::make_shared<engine::PositionItem>();
    // 持仓的 symbol 语义是交易对，不是币种：用 instId，缺失时才退回 ccy。
    item->symbol = pos_item.instId.empty() ? pos_item.ccy : pos_item.instId;
    item->volume = pos_item.pos;   // 持仓数量
    item->price = pos_item.avgPx;  // 均价
    item->pnl = pos_item.pnl;      // 盈亏

    // 转换持仓方向：long转为BUY，short转为SELL
    pos_item.posSide == "long" ? item->direction = engine::Direction::BUY : item->direction = engine::Direction::SELL;

    position_data->items.push_back(item);
  }

  co_await on_position(position_data);
  co_return;
}

// 查询订单信息，通过HTTP API获取并转换为统一格式
asio::awaitable<void> Okx::query_order(engine::QueryOrderDataPtr data) {
  // 调用HTTP API获取订单数据
  auto orders = co_await http_.get_pending_orders();

  auto orders_data = std::make_shared<engine::OrderData>();

  // 遍历所有订单，转换为统一格式
  for (auto& order : orders) {
    auto order_item = std::make_shared<engine::OrderDataItem>();
    order_item->order_id = order.ordId;                                                              // 订单ID
    order_item->direction = order.side == "buy" ? engine::Direction::BUY : engine::Direction::SELL;  // 买卖方向
    order_item->price = order.px;                                                                    // 订单价格
    order_item->volume = order.sz;                                                                   // 订单数量
    order_item->filled_volume = order.accFillSz;                                                     // 已成交数量
    order_item->status =
        order.state == "live" ? engine::OrderStatus::PENDING : engine::OrderStatus::PARTIAL_FILLED;  // 订单状态

    orders_data->items.push_back(order_item);
  }

  co_await on_order(orders_data);
  co_return;
}

// 主运行循环，持续从WebSocket接收数据
asio::awaitable<void> Okx::run() {
  auto executor = co_await asio::this_coro::executor;

  // 心跳与两条链路同生命周期：任一路结束都要让整个网关停下来，
  // 否则半边链路死亡会被一直掩盖（组件表面仍在运行，实际已收不到该路数据）。
  auto group = asio::experimental::make_parallel_group(asio::co_spawn(executor, watch_public(), asio::deferred),
                                                       asio::co_spawn(executor, watch_private(), asio::deferred),
                                                       asio::co_spawn(executor, ping_loop(), asio::deferred));

  try {
    // wait_for_one：任一路结束就立即返回并取消其余两路。
    // 用 wait_for_all 的话要等到最后一路也结束，期间没有任何告警。
    co_await group.async_wait(asio::experimental::wait_for_one(), asio::use_awaitable);
  } catch (boost::system::system_error& e) {
    LOG(ERROR) << fmt::format("watch error: {}", e.what());
  } catch (std::runtime_error& e) {
    LOG(ERROR) << fmt::format("watch error: {}", e.what());
  } catch (...) {
    LOG(ERROR) << fmt::format("watch error: unknown error");
  }

  // 引擎主动停止（shutdown 已置位）属于正常退出，不报错
  if (stopped_.load()) {
    LOG(INFO) << "okx gateway stopped";
    co_return;
  }

  LOG(ERROR) << "okx 公共行情/私有推送/心跳任一路已退出，网关停止工作";
  stopped_.store(true);
  // 半边链路死亡等同于网关不可用：必须让引擎停下来，
  // 否则策略会在没有行情或没有订单回报的情况下继续运行。
  co_await stop_engine();

  co_return;
}

asio::awaitable<void> Okx::shutdown() {
  LOG(INFO) << "okx gateway 正在关闭...";
  stopped_.store(true);
  // 必须主动打断 WebSocket：两个 watch 协程多半正挂在 read() 上，
  // 只置标志位不会唤醒它们，心跳定时器也会一直续期。
  if (ws_public_) ws_public_->interrupt();
  if (ws_private_) ws_private_->interrupt();
  co_return;
}

asio::awaitable<void> Okx::watch_private() {
  auto executor = co_await asio::this_coro::executor;
  int retry_count = 0;
  constexpr int max_retry = 20;
  
  for (;;) {
    if (stopped_.load()) {
      LOG(INFO) << "watch_private: 收到停止信号，退出";
      co_return;
    }
    try {
      co_await ws_deal(ws_private_);
      retry_count = 0;  // 成功处理后重置重试计数
      // 成功读到一条消息后立即继续读取，
      // 否则会误走退避逻辑，把推送强制限速成每秒一条。
      continue;
    } catch (boost::system::system_error& e) {
      // 被取消（引擎停止，或并行组取消其余分支）时直接退出，不再退避重连
      if (e.code() == asio::error::operation_aborted) {
        LOG(INFO) << "watch_private: 已被取消，退出";
        co_return;
      }
      LOG(ERROR) << fmt::format("watch_private error: {}", e.what());
    } catch (std::runtime_error& e) {
      LOG(ERROR) << fmt::format("watch_private error: {}", e.what());
    } catch (std::exception& e) {
      LOG(ERROR) << fmt::format("watch_private error: {}", e.what());
    } catch (...) {
      LOG(ERROR) << fmt::format("watch_private error: unknown error");
    }

    // 指数退避重连：1s, 2s, 4s, 8s, ... 最大 60s
    retry_count++;
    if (retry_count > max_retry) {
      LOG(ERROR) << fmt::format("watch_private: 达到最大重试次数 {}，停止重连", max_retry);
      co_return;
    }
    auto delay = std::min(int64_t(1) << (retry_count - 1), int64_t(60));
    LOG(WARNING) << fmt::format("watch_private: {}s 后进行第 {} 次重试", delay, retry_count);
    boost::asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::seconds(delay));
    co_await timer.async_wait(asio::use_awaitable);

    if (stopped_.load()) co_return;
    // 只睡眠而不重建连接的话断线后永远不会恢复：
    // OkxWs::connect() 只在 market_init() 里调用一次，退避后必须重建并重新订阅。
    co_await reconnect();
  }

  co_return;
}

asio::awaitable<void> Okx::ws_deal(std::shared_ptr<OkxWs> ws) {
  if (!ws) {
    // 连接尚未建立（上次重建失败）：抛错交由上层退避重连
    throw std::runtime_error("okx ws is not initialized");
  }

  // 从WebSocket读取消息。
  // 必须读传入的连接：写死 ws_private_ 会让 watch_public 永远读私有通道，
  // 公共行情 books/tickers 永不被消费，且两路协程争抢同一条消息、路由错乱。
  auto msg = co_await ws->read();

  // 处理消息
  if (!msg.event.empty()) {
    if (msg.event == "error") {
      LOG(ERROR) << fmt::format("ws error code: {}, message: {}", msg.code, msg.msg);
    } else if (msg.event == "channel-conn-count") {
      LOG(INFO) << fmt::format("ws channel-conn-count: {}", msg.connCount);
    } else if (msg.event == "subscribe") {
      LOG(INFO) << fmt::format("ws subscribe: {}, channel: {}", msg.event, msg.arg.channel);
    } else {
      // 处理事件消息（如订阅成功）
      LOG(INFO) << fmt::format("ws event: {}", msg.event);
    }
  }

  if (msg.arg.channel == "account") {
    // 处理账户数据
    auto account = std::any_cast<std::vector<market::okx::Account>>(msg.data);
    if (account.empty()) {
      LOG(INFO) << fmt::format("ws account empty");
      co_return;
    }
    co_await deal_account(account[0]);
  } else if (msg.arg.channel == "positions") {
    // 处理持仓数据
    co_await deal_position(std::any_cast<std::vector<PositionDetail>>(msg.data));
  } else if (msg.arg.channel == "books") {
    // 处理订单簿数据
    co_await deal_book(msg.arg.instId, std::any_cast<std::vector<WsBook>>(msg.data));
  } else if (msg.arg.channel == "tickers") {
    // 处理Tick数据
    co_await deal_tick(msg.arg.instId, std::any_cast<std::vector<WsTick>>(msg.data));
  } else if (msg.arg.channel == "orders") {
    // 处理订单数据
    co_await deal_order(std::any_cast<std::vector<QueryOrderDetail>>(msg.data));
  } else {
    LOG(INFO) << fmt::format("unknown channel: {}", msg.arg.channel);
  }
}

asio::awaitable<void> Okx::watch_public() {
  auto executor = co_await asio::this_coro::executor;
  int retry_count = 0;
  constexpr int max_retry = 20;

  for (;;) {
    if (stopped_.load()) {
      LOG(INFO) << "watch_public: 收到停止信号，退出";
      co_return;
    }
    try {
      co_await ws_deal(ws_public_);
      retry_count = 0;  // 成功处理后重置重试计数
      // 成功读到一条消息后立即继续读取，
      // 否则会误走退避逻辑，把行情强制限速成每秒一条。
      continue;
    } catch (boost::system::system_error& e) {
      // 被取消（引擎停止，或并行组取消其余分支）时直接退出，不再退避重连
      if (e.code() == asio::error::operation_aborted) {
        LOG(INFO) << "watch_public: 已被取消，退出";
        co_return;
      }
      LOG(ERROR) << fmt::format("watch_public error: {}", e.what());
    } catch (std::runtime_error& e) {
      LOG(ERROR) << fmt::format("watch_public error: {}", e.what());
    } catch (std::exception& e) {
      LOG(ERROR) << fmt::format("watch_public error: {}", e.what());
    } catch (...) {
      LOG(ERROR) << fmt::format("watch_public error: unknown error");
    }

    // 指数退避重连：1s, 2s, 4s, 8s, ... 最大 60s
    retry_count++;
    if (retry_count > max_retry) {
      LOG(ERROR) << fmt::format("watch_public: 达到最大重试次数 {}，停止重连", max_retry);
      co_return;
    }
    auto delay = std::min(int64_t(1) << (retry_count - 1), int64_t(60));
    LOG(WARNING) << fmt::format("watch_public: {}s 后进行第 {} 次重试", delay, retry_count);
    boost::asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::seconds(delay));
    co_await timer.async_wait(asio::use_awaitable);

    if (stopped_.load()) co_return;
    // 与 watch_private 同：退避后必须重建连接，否则行情永久停滞。
    co_await reconnect();
  }

  co_return;
}

// 周期性发送应用层心跳：OKX 要求客户端 30s 内发送一次纯文本 "ping"，
// 超时未收到会被服务端主动断开，而断线在这里表现为推送静默停止。
asio::awaitable<void> Okx::ping_loop() {
  auto executor = co_await asio::this_coro::executor;
  while (!stopped_.load()) {
    boost::asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::seconds(kPingIntervalS));
    co_await timer.async_wait(asio::use_awaitable);
    if (stopped_.load()) break;

    // 两条连接各自都要发心跳
    try {
      if (ws_public_) co_await ws_public_->write_raw("ping");
      if (ws_private_) co_await ws_private_->write_raw("ping");
    } catch (const std::exception& e) {
      // 连接正在重建时通道已关闭，发送失败属预期，交给 watch_* 的退避重连处理
      LOG(WARNING) << fmt::format("send ws ping failed: {}", e.what());
    }
  }
  co_return;
}

// 处理WebSocket接收到的订单簿数据，转换为统一格式并发送到引擎
asio::awaitable<void> Okx::deal_book(const std::string& symbol, const std::vector<WsBook>& msg) {
  auto book = std::make_shared<engine::Book>();
  // 解析WebSocket消息中的订单簿数据
  auto book_data = msg;

  // 遍历所有订单簿快照
  for (auto& book_item : book_data) {
    auto item = std::make_shared<engine::Book>();
    item->symbol = symbol;              // 交易对
    item->exchange = name();            // 交易所
    item->timestamp_ms = book_item.ts;  // 时间戳

    // 转换买盘数据
    for (auto& bid : book_item.bids) {
      auto bid_item = engine::BookItem();
      bid_item.price = bid.price;  // 买价
      bid_item.volume = bid.size;  // 买量
      item->bids.push_back(bid_item);
    }

    // 转换卖盘数据
    for (auto& ask : book_item.asks) {
      auto ask_item = engine::BookItem();
      ask_item.price = ask.price;  // 卖价
      ask_item.volume = ask.size;  // 卖量
      item->asks.push_back(ask_item);
    }

    // 保存最新的订单簿，供关联到Tick数据
    markets_.apply([&item](std::map<std::string, SingleMarket>& map) { map[item->symbol].last_book = item; });
    // 发送订单簿数据到引擎
    co_await on_book(item);
  }

  co_return;
}

// 处理WebSocket接收到的Tick数据，转换为统一格式并发送到引擎
asio::awaitable<void> Okx::deal_tick(const std::string& symbol, const std::vector<WsTick>& msg) {
  auto tick = std::make_shared<engine::TickData>();
  // 解析WebSocket消息中的Tick数据
  auto tick_data = msg;

  // 遍历所有Tick数据
  for (auto& tick_item : tick_data) {
    auto item = std::make_shared<engine::TickData>();
    item->symbol = symbol;              // 交易对
    item->exchange = name();            // 交易所
    item->timestamp_ms = tick_item.ts;  // 时间戳

    // 最新成交信息
    item->last_price = tick_item.last;                   // 最新价
    item->last_volume = tick_item.lastSz;                // 最新成交量
    item->turnover = tick_item.lastSz * tick_item.last;  // 成交额

    // 24小时统计数据
    item->last_close_price = tick_item.open24h;  // 昨收价（使用24h开盘价）
    item->open_price = tick_item.open24h;        // 24h开盘价
    item->high_price = tick_item.high24h;        // 24h最高价
    item->low_price = tick_item.low24h;          // 24h最低价

    // 保存最新的Tick，供关联到订单簿数据
    markets_.apply([&item](std::map<std::string, SingleMarket>& map) {
      item->order_book = map[item->symbol].last_book;
      map[item->symbol].last_tick = item;
    });

    // 发送Tick数据到引擎
    co_await on_tick(item);
  }

  co_return;
}

asio::awaitable<void> Okx::deal_order(const std::vector<QueryOrderDetail>& msg) {
  if (msg.empty()) {
    co_return;
  }

  auto item = std::make_shared<engine::OrderData>();
  item->symbol = msg[0].instId;  // 交易对
  item->exchange = name();       // 交易所
  // 遍历所有订单数据
  for (auto& order_item : msg) {
    auto order_data_item = std::make_shared<engine::OrderDataItem>();
    order_data_item->order_id = order_item.ordId;
    order_data_item->price = order_item.px;  // 委托价，而非成交均价
    order_data_item->volume = order_item.sz;
    order_data_item->filled_volume = order_item.accFillSz;
    order_data_item->direction = order_item.side == "buy" ? engine::Direction::BUY : engine::Direction::SELL;

    if (order_item.state == "filled") {
      order_data_item->status = engine::OrderStatus::FILLED;
    } else if (order_item.state == "canceled") {
      order_data_item->status = engine::OrderStatus::CANCELLED;
    } else if (order_item.state == "partially_filled") {
      order_data_item->status = engine::OrderStatus::PARTIAL_FILLED;
    } else {
      order_data_item->status = engine::OrderStatus::PENDING;
    }

    item->items.push_back(order_data_item);
  }

  // 发送订单数据到引擎
  co_await on_order(item);

  co_return;
}

// 订阅订单簿数据，通过WebSocket发送订阅请求
asio::awaitable<void> Okx::subscribe_book(engine::SubscribeDataPtr data) {
  if (!data) co_return;
  // 记住订阅过的品种：连接重建后订阅关系会丢失，需要重新订阅
  subscribed_symbols_.insert(data->symbol);

  if (!ws_public_) {
    LOG(WARNING) << fmt::format("subscribe_book: 公共连接未就绪，{} 将在重连后补订阅", data->symbol);
    co_return;
  }

  auto sub_req = WsSubscibeRequest();
  sub_req.op = "subscribe";                  // 订阅操作
  sub_req.args = {{"books", data->symbol}};  // 订阅订单簿通道

  // 发送订阅请求到WebSocket
  co_await ws_public_->write(sub_req);
  co_return;
}

// 订阅Tick数据，通过WebSocket发送订阅请求
asio::awaitable<void> Okx::subscribe_tick(engine::SubscribeDataPtr data) {
  if (!data) co_return;
  subscribed_symbols_.insert(data->symbol);

  if (!ws_public_) {
    LOG(WARNING) << fmt::format("subscribe_tick: 公共连接未就绪，{} 将在重连后补订阅", data->symbol);
    co_return;
  }

  auto sub_req = WsSubscibeRequest();
  sub_req.op = "subscribe";                    // 订阅操作
  sub_req.args = {{"tickers", data->symbol}};  // 订阅Ticker通道

  // 发送订阅请求到WebSocket
  co_await ws_public_->write(sub_req);
  co_return;
}

// 初始化市场网关，连接WebSocket
asio::awaitable<void> Okx::market_init() {
  auto ctx = co_await asio::this_coro::executor;
  ws_public_ = std::make_shared<OkxWs>(ctx, 100);
  ws_private_ = std::make_shared<OkxWs>(ctx, 100, "/ws/v5/private");

  // 连接到OKX的WebSocket服务器
  co_await ws_public_->connect();
  LOG(INFO) << "ws public connected";

  co_await ws_private_->connect();
  LOG(INFO) << "ws private connected";

  co_await ws_private_login();
  LOG(INFO) << "ws private login";

  co_await ws_private_subscribe_account();
  LOG(INFO) << "ws private subscribe account";

  co_await ws_private_subscribe_position();
  LOG(INFO) << "ws private subscribe position";

  co_await ws_private_subscribe_order();
  LOG(INFO) << "ws private subscribe order";

  co_return;
}

// 退避结束后重建连接：先打断旧连接再重建，避免 fd 泄漏
asio::awaitable<void> Okx::reconnect() {
  // 公共/私有任一链路失败都会走到这里，而 market_init() 会同时重建两条连接。
  // 两路同时重连会互相打断刚建好的连接，用该标志保证同一时刻只有一路在建。
  if (reconnecting_.exchange(true)) {
    LOG(INFO) << "已有重连流程在进行，本次跳过重建";
    co_return;
  }

  try {
    // 先打断旧连接：否则旧 socket 停在 CLOSE-WAIT，每次重连泄漏一个 fd
    if (ws_public_) ws_public_->interrupt();
    if (ws_private_) ws_private_->interrupt();

    co_await market_init();
    co_await resubscribe();
    LOG(INFO) << "okx 已重建连接并重新订阅行情";
  } catch (const std::exception& e) {
    // market_init() 可能抛异常（连不上/登录失败），只记日志，交给下一次退避
    LOG(ERROR) << fmt::format("重建 okx 连接失败: {}", e.what());
  } catch (...) {
    LOG(ERROR) << "重建 okx 连接失败: unknown error";
  }

  reconnecting_.store(false);
  co_return;
}

// 重新订阅此前订阅过的行情品种
asio::awaitable<void> Okx::resubscribe() {
  for (const auto& symbol : subscribed_symbols_) {
    auto request = std::make_shared<engine::SubscribeData>();
    request->symbol = symbol;
    co_await subscribe_book(request);
    co_await subscribe_tick(request);
  }
  LOG(INFO) << fmt::format("okx 已重新订阅 {} 个品种", subscribed_symbols_.size());
  co_return;
}

// 发送订单
asio::awaitable<void> Okx::send_orders(engine::OrderDataPtr order) {
  if (!order || order->items.empty()) co_return;

  auto order_req = std::vector<SendOrderRequest>();
  // 与 order_req 一一对应的原始子单，失败时按它构造拒单回报
  auto req_items = std::vector<engine::OrderDataItemPtr>();
  for (auto& item : order->items) {
    if (!item) continue;
    order_req.push_back(to_send_order_request(item));
    req_items.push_back(item);
  }

  auto rejected = std::make_shared<engine::OrderData>();
  rejected->symbol = order->symbol;
  rejected->exchange = name();

  auto rsp = std::vector<SendOrderRspDetail>();
  // 协程的 catch 块里不能使用 co_await，因此这里只记录失败，回报放到 try 之后。
  bool request_failed = false;
  try {
    rsp = co_await http_.send_orders(order_req);
  } catch (const std::exception& e) {
    // 整笔请求失败（网络/鉴权/批量全部失败）：每个子单都要回报拒单，
    // 否则上层为它们冻结的资金与持仓永远不会释放。
    LOG(ERROR) << fmt::format("send orders failed: {}", e.what());
    request_failed = true;
  }
  if (request_failed) {
    for (auto& item : req_items) {
      rejected->items.push_back(make_rejected_item(item));
    }
    co_await on_order(rejected);
    co_return;
  }

  for (size_t i = 0; i < req_items.size(); ++i) {
    // 假设：批量下单的响应与请求按位置一一对应（OKX 约定）
    if (i >= rsp.size()) {
      LOG(ERROR) << fmt::format("send order failed: 缺少第 {} 个子单的响应", i);
      rejected->items.push_back(make_rejected_item(req_items[i]));
      continue;
    }
    auto& detail = rsp[i];
    if (detail.sCode != 0) {
      LOG(ERROR) << fmt::format("send order failed, order_id: {}, code: {}, msg: {}",
                                req_items[i]->order_id, detail.sCode, detail.sMsg);
      // 必须回报拒单：上层据此释放冻结额度，否则失败单会永久占用额度。
      rejected->items.push_back(make_rejected_item(req_items[i]));
      continue;
    }
    // 记下内部 order_id 到交易所 ordId/instId 的映射，撤单时要靠它翻译
    order_refs_[req_items[i]->order_id] = OrderRef{detail.instId, detail.ordId};
  }

  if (!rejected->items.empty()) {
    co_await on_order(rejected);
  }

  co_return;
}

// 构造拒单回报项：数量/价格沿用原子单，状态置 REJECTED
engine::OrderDataItemPtr Okx::make_rejected_item(const engine::OrderDataItemPtr& item) {
  auto rejected_item = std::make_shared<engine::OrderDataItem>();
  rejected_item->order_id = item->order_id;
  rejected_item->symbol = item->symbol;
  rejected_item->direction = item->direction;
  rejected_item->otype = item->otype;
  rejected_item->price = item->price;    // 沿用原委托价
  rejected_item->volume = item->volume;  // 沿用原委托量
  rejected_item->filled_volume = item->filled_volume;
  rejected_item->status = engine::OrderStatus::REJECTED;
  return rejected_item;
}

// 取消订单
asio::awaitable<void> Okx::cancel_order(engine::OrderDataPtr order) {
  if (!order || order->items.empty()) co_return;

  auto cancel_req = std::vector<CancelOrderRequest>();
  auto req_items = std::vector<engine::OrderDataItemPtr>();
  for (auto& item : order->items) {
    if (!item || item->order_id.empty()) continue;
    // 撤单请求只带引擎分配的内部 order_id，必须翻译成交易所的 instId + ordId
    auto ref_it = order_refs_.find(item->order_id);
    if (ref_it == order_refs_.end() || ref_it->second.inst_id.empty() ||
        ref_it->second.ord_id.empty()) {
      LOG(WARNING) << fmt::format("cancel order: 未找到订单 {} 对应的交易所订单号，跳过撤单",
                                  item->order_id);
      continue;
    }
    auto req = CancelOrderRequest();
    req.instId = ref_it->second.inst_id;
    req.ordId = ref_it->second.ord_id;
    cancel_req.push_back(req);
    req_items.push_back(item);
  }

  if (cancel_req.empty()) co_return;

  auto cancelled = std::make_shared<engine::OrderData>();
  cancelled->symbol = order->symbol;
  cancelled->exchange = name();

  try {
    auto rsp = co_await http_.cancel_orders(cancel_req);
    for (size_t i = 0; i < req_items.size(); ++i) {
      if (i >= rsp.size()) {
        LOG(ERROR) << fmt::format("cancel order failed: 缺少订单 {} 的响应", cancel_req[i].ordId);
        continue;
      }
      if (rsp[i].sCode != 0) {
        // 撤单失败不回报：订单可能仍在交易所挂着，
        // 回报 CANCELLED 会让上层误以为冻结额度已释放。
        LOG(ERROR) << fmt::format("cancel order failed, ordId: {}, code: {}, msg: {}",
                                  cancel_req[i].ordId, rsp[i].sCode, rsp[i].sMsg);
        continue;
      }
      auto item = std::make_shared<engine::OrderDataItem>();
      item->order_id = req_items[i]->order_id;
      item->symbol = req_items[i]->symbol;
      item->direction = req_items[i]->direction;
      item->otype = req_items[i]->otype;
      item->price = req_items[i]->price;
      item->volume = req_items[i]->volume;
      item->filled_volume = req_items[i]->filled_volume;
      item->status = engine::OrderStatus::CANCELLED;
      cancelled->items.push_back(item);

      // 订单已终结，移除映射，避免长跑时这张表只增不减
      order_refs_.erase(req_items[i]->order_id);
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << fmt::format("cancel order failed: {}", e.what());
  } catch (...) {
    LOG(ERROR) << "cancel order failed: unknown error";
  }

  // 回报撤单结果，让上层释放该单占用的资金/持仓冻结额度
  if (!cancelled->items.empty()) {
    co_await on_order(cancelled);
  }

  co_return;
}

SendOrderRequest Okx::to_send_order_request(engine::OrderDataItemPtr order) {
  bool is_spot = !order->symbol.contains("SWAP");

  if (is_spot) {
    return to_send_order_request_spot(order);
  } else {
    return to_send_order_request_swap(order);
  }
}

SendOrderRequest Okx::to_send_order_request_spot(engine::OrderDataItemPtr order) {
  auto req = SendOrderRequest();
  req.instId = order->symbol;
  req.side = order->direction == engine::Direction::BUY ? "buy" : "sell";

  if (order->otype == engine::OrderType::MARKET) {
    req.ordType = "market";
  } else if (order->otype == engine::OrderType::LIMIT) {
    req.ordType = "limit";
  }

  req.tdMode = "cash";

  req.px = order->price;
  req.sz = order->volume;

  req.tgtCcy = "base_ccy";

  return req;
}

SendOrderRequest Okx::to_send_order_request_swap(engine::OrderDataItemPtr order) {
  auto req = SendOrderRequest();
  req.instId = order->symbol;
  req.side = order->direction == engine::Direction::BUY ? "buy" : "sell";
  req.posSide = order->direction == engine::Direction::BUY ? "long" : "short";

  if (order->otype == engine::OrderType::MARKET) {
    req.ordType = "market";
  } else if (order->otype == engine::OrderType::LIMIT) {
    req.ordType = "limit";
  }

  req.tdMode = "cross";

  // 合约才支持只减仓；现货留空，避免向不兼容该字段的接口传参。
  if (order->reduce_only) req.reduceOnly = "true";

  req.px = order->price;
  req.sz = order->volume;

  return req;
}

asio::awaitable<void> Okx::ws_private_login() {
  auto sub_req = WsLoginRequest();
  sub_req.op = "login";  // 登录操作

  auto timestamp = std::to_string(Common::get_current_time_s());
  auto sign = get_sign(timestamp, okx_config->secret_key());

  // 创建登录参数
  WsLoginDetail login_detail;
  login_detail.apiKey = okx_config->api_key();
  login_detail.passphrase = okx_config->passphrase();
  login_detail.timestamp = timestamp;
  login_detail.sign = sign;

  sub_req.args = {login_detail};

  co_await ws_private_->write(sub_req);
}

asio::awaitable<void> Okx::ws_private_subscribe_account() {
  auto sub_req = WsSubscibeAccountRequest();
  sub_req.op = "subscribe";  // 订阅操作
  sub_req.args = {{"account"}};

  co_await ws_private_->write(sub_req);
}

asio::awaitable<void> Okx::ws_private_subscribe_position() {
  auto sub_req = WsSubscibePositionRequest();
  sub_req.op = "subscribe";  // 订阅操作
  sub_req.args = {{"positions", "SWAP"}};

  co_await ws_private_->write(sub_req);
}

asio::awaitable<void> Okx::ws_private_subscribe_order() {
  auto sub_req = WsSubscibeOrderRequest();
  sub_req.op = "subscribe";  // 订阅操作
  // 私有订单频道按 instType 订阅，现货与合约都要订阅，
  // 否则只覆盖单一品种时收不到另一品种的订单推送。
  sub_req.args = {{"orders", "SPOT"}, {"orders", "SWAP"}};

  co_await ws_private_->write(sub_req);
}

};  // namespace market::okx
