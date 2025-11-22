#include "okx.h"
#include <boost/asio/experimental/parallel_group.hpp>

namespace market::okx {

Okx::Okx(engine::EnginePtr engine)
    : base::Gateway(engine, "okx"), http_(), ws_private_("/ws/v5/private") {}

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
  account_data->balance = account.totalEq;        // 总资产
  account_data->exchange = name();                // 交易所名称
  account_data->timestamp_ms = account.uTime;     // 更新时间

  // 遍历各币种余额明细
  for (auto& item : account.details) {
    auto balance_item = std::make_shared<engine::BalanceItem>();
    balance_item->symbol = item.ccy;      // 币种
    balance_item->balance = item.eq;      // 余额
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
    item->symbol = pos_item.ccy;        // 交易对
    item->volume = pos_item.pos;        // 持仓数量
    item->price = pos_item.avgPx;       // 均价
    item->pnl = pos_item.pnl;           // 盈亏

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
    order_item->order_id = order.ordId;                                                                   // 订单ID
    order_item->direction = order.side == "buy" ? engine::Direction::BUY : engine::Direction::SELL;      // 买卖方向
    order_item->price = order.px;                                                                         // 订单价格
    order_item->volume = order.sz;                                                                        // 订单数量
    order_item->filled_volume = order.accFillSz;                                                          // 已成交数量
    order_item->status = order.state == "live" ? engine::OrderStatus::PENDING : engine::OrderStatus::PARTIAL_FILLED;  // 订单状态

    orders_data->items.push_back(order_item);
  }

  co_await on_order(orders_data);
  co_return;
}


// 主运行循环，持续从WebSocket接收数据
asio::awaitable<void> Okx::run() {
  auto executor = co_await asio::this_coro::executor;


  auto group = asio::experimental::make_parallel_group(
    asio::co_spawn(executor, watch_public(), asio::deferred),
    asio::co_spawn(executor, watch_private(), asio::deferred)
  );

  try {
    co_await group.async_wait(asio::experimental::wait_for_all(), asio::use_awaitable);
  } catch (boost::system::system_error &e) {
    LOG(ERROR) << fmt::format("watch error: {}", e.what());
  } catch (std::runtime_error &e) {
    LOG(ERROR) << fmt::format("watch error: {}", e.what());
  } catch (...) {
    LOG(ERROR) << fmt::format("watch error: unknown error");
  }

  co_return;
}

asio::awaitable<void> Okx::watch_private() {
  LOG(INFO) << fmt::format("watch_private");
  for (;;) {
    // 从WebSocket读取消息
    auto msg = co_await ws_private_.read();

    // 处理错误消息
    if (msg.event == "error") {
      LOG(ERROR) << fmt::format("ws error code: {}, message: {}", msg.code, msg.msg);
      continue;
    } else if (msg.event == "channel-conn-count") {
      LOG(INFO) << fmt::format("ws channel-conn-count: {}", msg.connCount);
    } else if (!msg.event.empty()) {
      // 处理事件消息（如订阅成功）
      LOG(INFO) << fmt::format("ws event: {}", msg.event);
      continue;
    }

    if (msg.arg.channel == "account") {
      // 处理账户数据
      co_await deal_account(std::any_cast<Account>(msg.data));
    } else if (msg.arg.channel == "positions") {
      // 处理持仓数据
      co_await deal_position(std::any_cast<std::vector<PositionDetail>>(msg.data));
    } else {
      LOG(INFO) << fmt::format("unknown channel: {}", msg.arg.channel);
    }
  }

  co_return;
}

asio::awaitable<void> Okx::watch_public() {
  for (;;) {
    // 从WebSocket读取消息
    auto msg = co_await ws_public_.read();

    // 处理错误消息
    if (msg.event == "error") {
      LOG(ERROR) << fmt::format("ws error code: {}, message: {}", msg.code, msg.msg);
      continue;
    } else if (!msg.event.empty()) {
      // 处理事件消息（如订阅成功）
      LOG(INFO) << fmt::format("ws event: {}", msg.event);
      continue;
    }

    // 根据通道类型分发数据
    if (msg.arg.channel == "books") {
      // 处理订单簿数据
      co_await deal_book(msg.arg.instId, std::any_cast<std::vector<WsBook>>(msg.data));
    } else if (msg.arg.channel == "tickers") {
      // 处理Tick数据
      co_await deal_tick(msg.arg.instId, std::any_cast<std::vector<WsTick>>(msg.data));
    } else {
      // 处理其他数据
      LOG(INFO) << fmt::format("unknown channel: {}", msg.arg.channel);
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
    item->symbol = symbol;          // 交易对
    item->exchange = name();                // 交易所
    item->timestamp_ms = book_item.ts;      // 时间戳
    
    // 转换买盘数据
    for (auto& bid : book_item.bids) {
      auto bid_item = engine::BookItem();
      bid_item.price = bid.price;     // 买价
      bid_item.volume = bid.size;     // 买量
      item->bids.push_back(bid_item);
    }

    // 转换卖盘数据
    for (auto& ask : book_item.asks) {
      auto ask_item = engine::BookItem();
      ask_item.price = ask.price;     // 卖价
      ask_item.volume = ask.size;     // 卖量
      item->asks.push_back(ask_item);
    }

    // 保存最新的订单簿，供关联到Tick数据
    markets_.apply([item](std::map<std::string, SingleMarket> map) {
      map[item->symbol].last_book = item;
    });
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
    item->symbol = symbol;          // 交易对
    item->exchange = name();                // 交易所
    item->timestamp_ms = tick_item.ts;      // 时间戳

    // 最新成交信息
    item->last_price = tick_item.last;                      // 最新价
    item->last_volume = tick_item.lastSz;                   // 最新成交量
    item->turnover = tick_item.lastSz * tick_item.last;     // 成交额

    // 24小时统计数据
    item->last_close_price = tick_item.open24h;  // 昨收价（使用24h开盘价）
    item->open_price = tick_item.open24h;        // 24h开盘价
    item->high_price = tick_item.high24h;        // 24h最高价
    item->low_price = tick_item.low24h;          // 24h最低价
    
    // 保存最新的Tick，供关联到订单簿数据
    markets_.apply([item](std::map<std::string, SingleMarket> map) {
      item->order_book = map[item->symbol].last_book;
      map[item->symbol].last_tick = item;
    });

    // 发送Tick数据到引擎
    co_await on_tick(item);
  }

  co_return;
}

// 订阅订单簿数据，通过WebSocket发送订阅请求
asio::awaitable<void> Okx::subscribe_book(engine::SubscribeDataPtr data) {
  auto sub_req = WsSubscibeRequest();
  sub_req.op = "subscribe";                      // 订阅操作
  sub_req.args = {{"books", data->symbol}};      // 订阅订单簿通道

  // 发送订阅请求到WebSocket
  co_await ws_public_.write(sub_req);
  co_return;
}

// 订阅Tick数据，通过WebSocket发送订阅请求
asio::awaitable<void> Okx::subscribe_tick(engine::SubscribeDataPtr data) {
  auto sub_req = WsSubscibeRequest();
  sub_req.op = "subscribe";                      // 订阅操作
  sub_req.args = {{"tickers", data->symbol}};    // 订阅Ticker通道

  // 发送订阅请求到WebSocket
  co_await ws_public_.write(sub_req);
  co_return;
}

// 初始化市场网关，连接WebSocket
asio::awaitable<void> Okx::market_init() {
  // 连接到OKX的WebSocket服务器
  co_await ws_public_.connect();
  LOG(INFO) << "ws public connected";

  co_await ws_private_.connect();
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

// 发送订单
asio::awaitable<void> Okx::send_orders(engine::OrderDataPtr order) {
  auto order_req = std::vector<SendOrderRequest>();
  for (auto &item : order->items) {
    bool is_spot = !item->symbol.contains("SWAP");

    auto req = SendOrderRequest();
    req.instId = item->symbol;
    req.side = item->direction == engine::Direction::BUY ? "buy" : "sell";
    if (!is_spot) {
      req.posSide = item->direction == engine::Direction::BUY ? "long" : "short";
    }
    if (item->otype == engine::OrderType::MARKET) {
      req.ordType = "market";
    } else if (item->otype == engine::OrderType::LIMIT) {
      req.ordType = "limit";
    }
    if (is_spot) {
      req.tdMode = "cash";
    } else {
      req.tdMode = "cross";
    }
    
    req.px = item->price;
    req.sz = item->volume;

    if (is_spot) {
      req.tgtCcy = "base_ccy";
    }

    order_req.push_back(req);
  }

  auto rsp = co_await http_.send_orders(order_req);

  for (auto &item : rsp) {
    if (item.sCode != 0) {
      LOG(ERROR) << "send order failed, code: " << item.sCode << ", msg: " << item.sMsg;
    }
  }

  co_return;
}

asio::awaitable<void> Okx::ws_private_login() {
  auto sub_req = WsLoginRequest();
  sub_req.op = "login";                      // 登录操作

  auto timestamp = std::to_string(Common::get_current_time_s());
  auto sign = get_sign(timestamp, okx_config->secret_key());
  sub_req.args = {{okx_config->api_key(), okx_config->passphrase(), timestamp, sign}};    // 订阅Ticker通道

  co_await ws_private_.write(sub_req);
}

asio::awaitable<void> Okx::ws_private_subscribe_account() {
  auto sub_req = WsSubscibeRequest();
  sub_req.op = "subscribe";                      // 订阅操作
  sub_req.args = {{"account"}};

  co_await ws_private_.write(sub_req);
}

asio::awaitable<void> Okx::ws_private_subscribe_position() {
  auto sub_req = WsSubscibePositionRequest();
  sub_req.op = "subscribe";                      // 订阅操作
  sub_req.args = {{"positions", "SWAP"}};

  co_await ws_private_.write(sub_req);
}

asio::awaitable<void> Okx::ws_private_subscribe_order() {
  auto sub_req = WsSubscibeOrderRequest();
  sub_req.op = "subscribe";                      // 订阅操作
  sub_req.args = {{"orders", "SWAP"}};

  co_await ws_private_.write(sub_req);
}

};  // namespace market::okx
