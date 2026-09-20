#include "backtest_execution_venue.h"

#include <utility>

#include <fmt/core.h>
#include <glog/logging.h>

namespace core::execution {

namespace {
engine::Direction toEngineDirection(domain::Side side) {
  return side == domain::Side::BUY ? engine::Direction::BUY : engine::Direction::SELL;
}

engine::OrderType toEngineOrderType(domain::OrderType type) {
  return type == domain::OrderType::MARKET ? engine::OrderType::MARKET
                                           : engine::OrderType::LIMIT;
}
}  // namespace

BacktestExecutionVenue::BacktestExecutionVenue()
    : m_owned_engine(std::make_unique<backtest::match::MatchEngine>()) {
  // 撮合器内部会直接修改订单项状态，这里只负责把状态转换为标准化执行回报。
  m_match_engine = m_owned_engine.get();
}

domain::CommandResult BacktestExecutionVenue::submit(
    const std::string& order_id, const domain::OrderIntent& intent) {
  if (order_id.empty() || intent.symbol.empty() || intent.quantity <= 0) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid backtest order"}, order_id};
  }
  if (m_intents.contains(order_id)) {
    return {false, {domain::ErrorCode::DUPLICATE, "order already exists"}, order_id};
  }
  if (intent.type == domain::OrderType::LIMIT && intent.price <= 0) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "limit price is invalid"}, order_id};
  }
  if (intent.type == domain::OrderType::MARKET && m_current_price <= 0) {
    return {false, {domain::ErrorCode::VENUE_ERROR, "market price is unavailable"}, order_id};
  }

  // 卖出量不得超过可用持仓（已扣除其他未成交卖单占用的部分）。
  // 撮合器不跟踪持仓，缺少这层约束就会裸卖空：持仓穿成巨额负数，
  // 净值、绩效与训练奖励都会彻底失真。
  dec_float quantity = intent.quantity;
  if (intent.side == domain::Side::SELL) {
    const dec_float held = m_positions[intent.symbol];
    const dec_float reserved = m_reserved_sell[intent.symbol];
    const dec_float available = held > reserved ? held - reserved : dec_float(0);
    // 拒单原因必须可见：这类无效请求在长回测里会累积上万次，
    // 缺少 held/reserved 明细就只能靠猜。
    VLOG(1) << fmt::format("[回测端口] 卖出校验 order={} qty={} held={} reserved={} available={}",
                           order_id, quantity.str(), held.str(), reserved.str(),
                           available.str());
    if (available <= 0) {
      VLOG(1) << fmt::format("[回测端口] 卖出被拒 order={}: 无可用持仓 (held={}, reserved={})",
                             order_id, held.str(), reserved.str());
      return {false,
              {domain::ErrorCode::INVALID_ARGUMENT, "no available position to sell"},
              order_id};
    }
    if (quantity > available) quantity = available;
    m_reserved_sell[intent.symbol] += quantity;
  }

  // 记录裁剪后的意图，后续回报与持仓结算都以实际下单量为准。
  domain::OrderIntent submitted = intent;
  submitted.quantity = quantity;
  m_intents.emplace(order_id, submitted);

  auto order = std::make_shared<engine::OrderData>();
  order->symbol = intent.symbol;
  order->exchange = "backtest-venue";
  auto item = std::make_shared<engine::OrderDataItem>();
  item->order_id = order_id;
  item->symbol = intent.symbol;
  item->direction = toEngineDirection(intent.side);
  item->otype = toEngineOrderType(intent.type);
  item->price = intent.type == domain::OrderType::MARKET ? m_current_price : intent.price;
  item->volume = quantity;
  item->status = engine::OrderStatus::SUBMITTING;
  item->reduce_only = intent.reduce_only;
  order->items.push_back(item);

  matchEngine().submitOrder(order, m_current_price);
  // 市价单与可即时撮合的限价单会在 submitOrder 内直接成交，必须立刻结算：
  // 一是同步持仓与卖单冻结额度，二是让 FILL 回报只由 settleTrades 发出一次，
  // 否则这里再发一条 FILL 会与结算回报重复，账本凭空多出一笔成交。
  settleTrades();
  if (m_intents.contains(order_id)) {
    emit({mapStatus(item->status), std::to_string(++m_report_counter), order_id,
          intent.intent_id, intent.symbol, intent.side, intent.type, item->price,
          quantity, item->filled_volume, 0, 0, {}});
  }
  return {true, {}, order_id};
}

domain::CommandResult BacktestExecutionVenue::cancel(const std::string& order_id) {
  if (order_id.empty()) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "order id is empty"}, order_id};
  }
  const auto cancelled = matchEngine().cancelOrder(order_id);
  if (!cancelled) {
    return {false, {domain::ErrorCode::NOT_FOUND, "pending order not found"}, order_id};
  }
  const auto it = m_intents.find(order_id);
  const domain::OrderIntent intent = it != m_intents.end() ? it->second : domain::OrderIntent{};
  // 撤销卖单要归还其占用的持仓额度，否则可用持仓会被逐步耗尽。
  if (it != m_intents.end() && intent.side == domain::Side::SELL) {
    dec_float& reserved = m_reserved_sell[intent.symbol];
    reserved -= intent.quantity;
    if (reserved < 0) reserved = dec_float(0);
    VLOG(1) << fmt::format("[回测端口] 撤单归还额度 order={} qty={} -> reserved={}",
                           order_id, intent.quantity.str(), reserved.str());
  }
  emit({domain::ExecutionEventType::CANCELLED, std::to_string(++m_report_counter),
        order_id, intent.intent_id, intent.symbol, intent.side, intent.type,
        intent.price, intent.quantity, 0, 0, 0, {}});
  m_intents.erase(order_id);
  return {true, {}, order_id};
}

VenueCapabilities BacktestExecutionVenue::capabilities() const {
  return {.supports_cancel = true, .supports_replace = false,
          .supports_batch_orders = false, .supports_partial_fill = false};
}

void BacktestExecutionVenue::setCallback(ExecutionCallback callback) {
  m_callback = std::move(callback);
}

void BacktestExecutionVenue::onMarketPrice(const std::string& symbol,
                                           const dec_float& price,
                                           int64_t timestamp_ms) {
  if (price <= 0) return;
  m_current_price = price;

  auto tick = std::make_shared<engine::TickData>();
  tick->symbol = symbol;
  tick->exchange = "backtest-venue";
  tick->timestamp_ms = timestamp_ms;
  tick->last_price = price;

  matchEngine().onTick(tick);
  settleTrades();
}

void BacktestExecutionVenue::settleTrades() {
  const auto& trades = matchEngine().trades();
  for (std::size_t i = m_trade_cursor; i < trades.size(); ++i) {
    const auto& trade = trades[i];
    if (!trade || !trade->order) continue;
    for (const auto& item : trade->order->items) {
      if (!item || item->order_id.empty()) continue;
      const auto it = m_intents.find(item->order_id);
      if (it == m_intents.end()) continue;
      // 成交后结算持仓，并释放该卖单占用的冻结额度。
      if (it->second.side == domain::Side::BUY) {
        m_positions[it->second.symbol] += trade->volume;
      } else {
        m_positions[it->second.symbol] -= trade->volume;
        dec_float& reserved = m_reserved_sell[it->second.symbol];
        reserved -= it->second.quantity;
        if (reserved < 0) reserved = dec_float(0);
      }
      VLOG(1) << fmt::format(
          "[回测端口] 成交结算 order={} side={} volume={} -> held={} reserved={}",
          item->order_id,
          it->second.side == domain::Side::BUY ? "BUY" : "SELL", trade->volume.str(),
          m_positions[it->second.symbol].str(),
          m_reserved_sell[it->second.symbol].str());

      // 手续费必须写进执行回报：统一账本据此累计成本，策略读到的现金才会反映
      // 真实成交成本。此前这里恒为 0，导致训练在零成本下进行，学不会规避手续费。
      const bool is_maker = it->second.type == domain::OrderType::LIMIT;
      const dec_float fee = calcFee(trade->price * trade->volume, is_maker);
      emit({domain::ExecutionEventType::FILL, std::to_string(++m_report_counter),
            item->order_id, it->second.intent_id, it->second.symbol, it->second.side,
            it->second.type, trade->price, it->second.quantity, trade->volume, fee,
            trade->timestamp_ms, {}});
      m_intents.erase(it);
    }
  }
  m_trade_cursor = trades.size();
}

void BacktestExecutionVenue::emit(const domain::ExecutionReport& report) {
  if (m_callback) m_callback(report);
}

dec_float BacktestExecutionVenue::calcFee(const dec_float& turnover, bool is_maker) const {
  if (turnover <= 0) return dec_float(0);
  return turnover * (is_maker ? m_maker_fee_rate : m_taker_fee_rate);
}

domain::ExecutionEventType BacktestExecutionVenue::mapStatus(engine::OrderStatus status) {
  switch (status) {
    case engine::OrderStatus::SUBMITTING: return domain::ExecutionEventType::ACCEPTED;
    case engine::OrderStatus::PENDING: return domain::ExecutionEventType::PENDING;
    case engine::OrderStatus::PARTIAL_FILLED: return domain::ExecutionEventType::PARTIAL_FILL;
    case engine::OrderStatus::FILLED: return domain::ExecutionEventType::FILL;
    case engine::OrderStatus::CANCELLED: return domain::ExecutionEventType::CANCELLED;
    case engine::OrderStatus::REJECTED: return domain::ExecutionEventType::REJECTED;
  }
  return domain::ExecutionEventType::REJECTED;
}

}  // namespace core::execution
