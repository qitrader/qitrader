#include "backtest_gateway.h"

#include <glog/logging.h>

#include "data/csv_loader.h"

namespace backtest::base {

BacktestGateway::BacktestGateway(engine::EnginePtr engine, const std::string& data_file,
                                 const std::string& start_date, const std::string& end_date,
                                 const dec_float& initial_capital,
                                 const dec_float& maker_fee_rate,
                                 const dec_float& taker_fee_rate)
    : Gateway(engine, "backtest"),
      m_data_file(data_file),
      m_start_date(start_date),
      m_end_date(end_date),
      m_initial_capital(initial_capital),
      m_cash(initial_capital),
      m_position_volume(0),
      m_position_avg_price(0),
      m_last_price(0),
      m_maker_fee_rate(maker_fee_rate),
      m_taker_fee_rate(taker_fee_rate),
      m_analyzer(initial_capital) {
  // 设置撮合引擎的成交回调
  m_match_engine.setOnTrade(
      [this](auto order, auto trade) { onTradeEvent(order, trade); });
}

asio::awaitable<void> BacktestGateway::run() {
  LOG(INFO) << "============================================";
  LOG(INFO) << "           回测模式启动";
  LOG(INFO) << fmt::format("数据文件:     {}", m_data_file);
  LOG(INFO) << fmt::format("初始资金:     {}", m_initial_capital.str(2, std::ios_base::fixed));
  if (!m_start_date.empty()) {
    LOG(INFO) << fmt::format("开始日期:     {}", m_start_date);
  }
  if (!m_end_date.empty()) {
    LOG(INFO) << fmt::format("结束日期:     {}", m_end_date);
  }
  LOG(INFO) << "============================================";

  data::CsvLoader loader(m_data_file, m_start_date, m_end_date);
  if (!loader.isValid()) {
    LOG(ERROR) << "数据文件不可读: " << m_data_file;
    co_await stop_engine();
    co_return;
  }

  auto ticks = loader.loadTicks();
  auto bars = ticks.empty() ? loader.loadBars()
                            : std::vector<std::shared_ptr<engine::BarData>>{};
  if (ticks.empty() && bars.empty()) {
    LOG(ERROR) << "未加载到任何 Tick 或 K 线数据";
    co_await stop_engine();
    co_return;
  }

  if (!ticks.empty()) {
    LOG(INFO) << fmt::format("开始回放 {} 条 Tick 数据...", ticks.size());
    // 每次只推进一条 Tick，等待策略和该 Tick 触发的订单处理完成。
    for (size_t i = 0; i < ticks.size(); ++i) {
      auto& tick = ticks[i];
      if (!m_subscribed_symbols.empty() &&
          m_subscribed_symbols.find(tick->symbol) == m_subscribed_symbols.end()) {
        continue;
      }

      m_last_price = tick->last_price;
      // 先转发行情，再撮合：运行时据此拿到本帧快照，撮合结果随后才通知策略。
      if (m_tick_sink) m_tick_sink(tick);
      m_match_engine.onTick(tick);
      co_await flushOrderNotifications();
      co_await on_tick_sync(tick);

      if (i % 100 == 0 || i == ticks.size() - 1) {
        m_analyzer.addEquitySnapshot(calcEquity(m_last_price), tick->timestamp_ms);
      }
    }
  } else {
    LOG(INFO) << fmt::format("开始回放 {} 条 K 线数据...", bars.size());
    for (size_t i = 0; i < bars.size(); ++i) {
      auto& bar = bars[i];
      if (!m_subscribed_symbols.empty() &&
          m_subscribed_symbols.find(bar->symbol) == m_subscribed_symbols.end()) {
        continue;
      }

      m_last_price = bar->close_price;
      if (m_bar_sink) m_bar_sink(bar);
      auto tick = std::make_shared<engine::TickData>();
      tick->symbol = bar->symbol;
      tick->exchange = bar->exchange;
      tick->timestamp_ms = bar->timestamp_ms;
      tick->last_price = bar->close_price;
      m_match_engine.onTick(tick);
      co_await flushOrderNotifications();
      co_await on_bar_sync(bar);

      if (i % 100 == 0 || i == bars.size() - 1) {
        m_analyzer.addEquitySnapshot(calcEquity(m_last_price), bar->timestamp_ms);
      }
    }
  }

  co_await flushOrderNotifications();
  dec_float final_equity = calcEquity(m_last_price);
  m_analyzer.report(final_equity);

  LOG(INFO) << "回测完成，正在停止引擎...";
  co_await stop_engine();
}

asio::awaitable<void> BacktestGateway::market_init() {
  LOG(INFO) << "回测网关初始化完成";
  co_return;
}

void BacktestGateway::unsubscribe(const std::string& symbol) {
  m_subscribed_symbols.erase(symbol);
}

asio::awaitable<void> BacktestGateway::send_orders(engine::OrderDataPtr order) {
  if (!order || order->items.empty()) co_return;

  auto mutable_order = std::const_pointer_cast<engine::OrderData>(order);
  dec_float required_cash(0);
  dec_float required_position(0);
  dec_float reserve_cash(0);
  dec_float reserve_position(0);
  bool valid = true;

  for (const auto& item : mutable_order->items) {
    if (!item || item->status != engine::OrderStatus::SUBMITTING || item->volume <= 0) {
      valid = false;
      break;
    }

    const dec_float execution_price = item->otype == engine::OrderType::MARKET
                                          ? m_last_price
                                          : item->price;
    const std::string symbol = item->symbol.empty() ? mutable_order->symbol : item->symbol;
    if (symbol.empty() || execution_price <= 0) {
      valid = false;
      break;
    }

    if (item->direction == engine::Direction::BUY) {
      required_cash += execution_price * item->volume;
      if (item->otype == engine::OrderType::LIMIT) {
        reserve_cash += item->price * item->volume;
      }
    } else {
      if (!m_position_symbol.empty() && symbol != m_position_symbol) {
        valid = false;
        break;
      }
      required_position += item->volume;
      if (item->otype == engine::OrderType::LIMIT) {
        reserve_position += item->volume;
      }
    }
  }

  if (required_cash > m_cash - m_reserved_cash ||
      required_position > m_position_volume - m_reserved_position_volume) {
    valid = false;
  }

  if (!valid) {
    for (const auto& item : mutable_order->items) {
      if (item) {
        auto mutable_item = std::const_pointer_cast<engine::OrderDataItem>(item);
        mutable_item->status = engine::OrderStatus::REJECTED;
        mutable_item->filled_volume = dec_float(0);
      }
    }
    co_await on_order(cloneOrder(order));
    co_return;
  }

  m_reserved_cash += reserve_cash;
  m_reserved_position_volume += reserve_position;
  m_match_engine.submitOrder(mutable_order, m_last_price);

  bool has_filled_item = false;
  for (const auto& item : mutable_order->items) {
    if (item && item->status == engine::OrderStatus::FILLED) {
      has_filled_item = true;
      break;
    }
  }

  if (has_filled_item) {
    co_await flushOrderNotifications();
  } else {
    co_await on_order(cloneOrder(order));
  }
}

asio::awaitable<void> BacktestGateway::cancel_order(engine::OrderDataPtr order) {
  if (!order) co_return;

  for (const auto& item : order->items) {
    if (!item || item->order_id.empty()) continue;
    auto cancelled = m_match_engine.cancelOrder(item->order_id);
    if (!cancelled) continue;

    for (const auto& cancelled_item : cancelled->items) {
      if (!cancelled_item || cancelled_item->order_id != item->order_id) continue;
      if (cancelled_item->direction == engine::Direction::BUY &&
          cancelled_item->otype == engine::OrderType::LIMIT) {
        m_reserved_cash -= cancelled_item->price *
            (cancelled_item->volume - cancelled_item->filled_volume);
        if (m_reserved_cash < 0) m_reserved_cash = dec_float(0);
      } else if (cancelled_item->direction == engine::Direction::SELL &&
                 cancelled_item->otype == engine::OrderType::LIMIT) {
        m_reserved_position_volume -= cancelled_item->volume -
            cancelled_item->filled_volume;
        if (m_reserved_position_volume < 0) {
          m_reserved_position_volume = dec_float(0);
        }
      }
    }
    co_await on_order(cloneOrder(cancelled));
  }
}

asio::awaitable<void> BacktestGateway::query_account(engine::QueryAccountDataPtr data) {
  auto account = std::make_shared<engine::AccountData>();
  account->account_id = "backtest";
  account->exchange = "backtest";
  account->balance = m_cash;
  account->frozen_balance = m_reserved_cash;

  auto balance_item = std::make_shared<engine::BalanceItem>();
  balance_item->symbol = "USDT";
  balance_item->balance = m_cash - m_reserved_cash;
  balance_item->frozen_balance = m_reserved_cash;
  account->items.push_back(balance_item);

  co_await on_account(account);
}

asio::awaitable<void> BacktestGateway::query_position(engine::QueryPositionDataPtr data) {
  auto position = std::make_shared<engine::PositionData>();
  position->exchange = "backtest";

  if (m_position_volume > 0) {
    auto item = std::make_shared<engine::PositionItem>();
    item->symbol = m_position_symbol;
    item->volume = m_position_volume;
    item->direction = engine::Direction::BUY;
    item->frozen_volume = m_reserved_position_volume;
    item->price = m_position_avg_price;
    item->pnl = (m_last_price - m_position_avg_price) * m_position_volume;
    position->items.push_back(item);
  }

  co_await on_position(position);
}

asio::awaitable<void> BacktestGateway::query_order(engine::QueryOrderDataPtr data) {
  // 回测模式暂不支持历史订单查询
  co_return;
}

asio::awaitable<void> BacktestGateway::subscribe_book(engine::SubscribeDataPtr data) {
  if (data) {
    m_subscribed_symbols.insert(data->symbol);
    LOG(INFO) << "[回测] 订阅订单簿: " << data->symbol;
  }
  co_return;
}

asio::awaitable<void> BacktestGateway::subscribe_tick(engine::SubscribeDataPtr data) {
  if (data) {
    m_subscribed_symbols.insert(data->symbol);
    LOG(INFO) << "[回测] 订阅 Tick: " << data->symbol;
  }
  co_return;
}

void BacktestGateway::onTradeEvent(std::shared_ptr<engine::OrderData> order,
                                   std::shared_ptr<engine::TradeData> trade) {
  if (!trade) return;

  // 记录成交到分析器
  m_analyzer.addTrade(trade);

  // 释放该限价单占用的冻结资金或持仓；市价单没有预冻结。
  bool is_limit_order = false;
  if (trade->order) {
    for (const auto& item : trade->order->items) {
      if (item && item->direction == trade->direction &&
          item->otype == engine::OrderType::LIMIT) {
        is_limit_order = true;
        break;
      }
    }
  }
  if (is_limit_order) {
    if (trade->direction == engine::Direction::BUY) {
      m_reserved_cash -= trade->price * trade->volume;
      if (m_reserved_cash < 0) m_reserved_cash = dec_float(0);
    } else {
      m_reserved_position_volume -= trade->volume;
      if (m_reserved_position_volume < 0) {
        m_reserved_position_volume = dec_float(0);
      }
    }
  }

  // 更新账户和持仓
  if (trade->direction == engine::Direction::BUY) {
    dec_float cost = trade->price * trade->volume;
    // Runtime 路径的订单直接进共享撮合器，绕过了 send_orders 的资金校验，
    // 必须在此限制买入量：否则现金会被透支成巨额负数，净值彻底失真。
    // 限制时把手续费一并计入单价，保证扣款后现金不会穿负。
    const dec_float rate = is_limit_order ? m_maker_fee_rate : m_taker_fee_rate;
    dec_float volume = trade->volume;
    if (m_cash <= 0) {
      volume = dec_float(0);
    } else if (trade->price > 0) {
      const dec_float unit_cost = trade->price * (dec_float(1) + rate);
      const dec_float affordable = m_cash / unit_cost;
      if (volume > affordable) volume = affordable;
    }
    cost = trade->price * volume;

    // 手续费必须与 Paper 保持一致：否则策略在零成本环境里训练，
    // 学出的行为不会规避手续费，部署到收费环境必然亏损。
    const dec_float fee = calcFee(cost, is_limit_order);
    m_fees += fee;
    m_cash -= cost + fee;

    // 更新持仓均价（含买入手续费）
    dec_float total_cost = m_position_avg_price * m_position_volume + cost + fee;
    m_position_volume += volume;
    if (m_position_volume > 0) {
      m_position_avg_price = total_cost / m_position_volume;
    }
    m_position_symbol = trade->symbol;
  } else {
    // 卖出。Runtime 路径的订单直接进共享撮合器，不经过 send_orders 的持仓校验，
    // 所以这里必须再限制一次：否则持仓会穿成巨额负数，净值与绩效彻底失真。
    const dec_float sellable = std::min(trade->volume, m_position_volume);
    dec_float revenue = trade->price * sellable;
    const dec_float fee = calcFee(revenue, is_limit_order);
    m_fees += fee;
    m_cash += revenue - fee;
    m_position_volume -= sellable;

    if (m_position_volume <= 0) {
      m_position_volume = dec_float(0);
      m_position_avg_price = dec_float(0);
    }
  }

  if (order) {
    m_pending_order_notifications.push_back(cloneOrder(order));
  }
}

std::shared_ptr<engine::OrderData> BacktestGateway::cloneOrder(engine::OrderDataPtr order) const {
  if (!order) return nullptr;

  auto copy = std::make_shared<engine::OrderData>();
  copy->symbol = order->symbol;
  copy->exchange = order->exchange;
  copy->timestamp_ms = order->timestamp_ms;
  for (const auto& item : order->items) {
    if (!item) continue;
    auto item_copy = std::make_shared<engine::OrderDataItem>();
    item_copy->symbol = item->symbol;
    item_copy->exchange = item->exchange;
    item_copy->timestamp_ms = item->timestamp_ms;
    item_copy->order_id = item->order_id;
    item_copy->direction = item->direction;
    item_copy->price = item->price;
    item_copy->volume = item->volume;
    item_copy->filled_volume = item->filled_volume;
    item_copy->otype = item->otype;
    item_copy->status = item->status;
    copy->items.push_back(item_copy);
  }
  return copy;
}

asio::awaitable<void> BacktestGateway::flushOrderNotifications() {
  // 逐条取出后发送：回调期间可能再次写回待通知队列，
  // 因此不能使用范围遍历，避免容器重分配导致迭代器或引用失效。
  while (!m_pending_order_notifications.empty()) {
    auto order = m_pending_order_notifications.front();
    m_pending_order_notifications.erase(m_pending_order_notifications.begin());
    if (order) co_await on_order(order);
  }
}

dec_float BacktestGateway::calcEquity(const dec_float& current_price) const {
  return m_cash + m_position_volume * current_price;
}

dec_float BacktestGateway::calcFee(const dec_float& turnover, bool is_maker) const {
  if (turnover <= 0) return dec_float(0);
  return turnover * (is_maker ? m_maker_fee_rate : m_taker_fee_rate);
}

}  // namespace backtest::base
