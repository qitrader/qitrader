#include "match_engine.h"

#include <glog/logging.h>

namespace backtest::match {

namespace {
/// 买侧：价格不劣于限价的买档累计量（这些档位必须先被吃掉才轮到我们）
dec_float accumulatedBids(const engine::BookPtr& book, const dec_float& price) {
  dec_float total(0);
  if (!book) return total;
  for (const auto& level : book->bids) {
    if (level.price >= price) total += level.volume;
  }
  return total;
}

/// 卖侧：价格不优于限价的卖档累计量
dec_float accumulatedAsks(const engine::BookPtr& book, const dec_float& price) {
  dec_float total(0);
  if (!book) return total;
  for (const auto& level : book->asks) {
    if (level.price <= price) total += level.volume;
  }
  return total;
}
}  // namespace

void MatchEngine::submitOrder(std::shared_ptr<engine::OrderData> order,
                              const dec_float& current_price) {
  if (!order || order->items.empty()) return;

  for (auto& item : order->items) {
    // 为订单项生成 ID（如果没有的话）
    auto mutable_item = std::const_pointer_cast<engine::OrderDataItem>(item);
    if (mutable_item->order_id.empty()) {
      mutable_item->order_id = generateId();
    }

    if (mutable_item->otype == engine::OrderType::MARKET) {
      // 市价单必须依赖有效的最新行情，禁止在尚未收到 Tick 时以零价格成交。
      if (current_price <= 0) {
        mutable_item->filled_volume = dec_float(0);
        mutable_item->status = engine::OrderStatus::REJECTED;
        LOG(WARNING) << fmt::format("[撮合] 拒绝市价单 {}：当前行情价格无效 ({})",
                                    mutable_item->order_id, current_price.str());
        continue;
      }

      // 市价单：以当前价格立即成交
      mutable_item->price = current_price;
      mutable_item->filled_volume = mutable_item->volume;
      mutable_item->status = engine::OrderStatus::FILLED;

      // 生成成交记录
      auto trade = std::make_shared<engine::TradeData>();
      trade->trade_id = generateId();
      trade->symbol = order->symbol.empty() ? mutable_item->symbol : order->symbol;
      trade->exchange = "backtest";
      trade->timestamp_ms = order->timestamp_ms;
      trade->direction = mutable_item->direction;
      trade->price = current_price;
      trade->volume = mutable_item->volume;
      trade->order = order;
      m_trades.push_back(trade);

      VLOG(1) << fmt::format("[回测撮合] 市价单成交: {} {} {} @ {}",
                             mutable_item->order_id,
                               mutable_item->direction == engine::Direction::BUY ? "买入" : "卖出",
                               mutable_item->volume.str(), current_price.str());

      if (m_on_trade) {
        m_on_trade(order, trade);
      }
    } else {
      // 限价单：先估算前置排队量（用最近一次盘口兜底），再尝试即时成交。
      // 刚挂单时还没有任何主动成交量能推进队列，故 aggressive_volume 传 0。
      m_queue_ahead[mutable_item->order_id] =
          computeQueueAhead(m_last_book, mutable_item->direction, mutable_item->price,
                            mutable_item->volume);
      if (!tryMatch(order, current_price, order->timestamp_ms, order->symbol, m_last_book,
                    dec_float(0))) {
        mutable_item->status = engine::OrderStatus::PENDING;
        m_pending_orders.push_back(order);
        VLOG(1) << fmt::format("[回测撮合] 限价单挂单: {} {} {} @ {}",
                                 mutable_item->order_id,
                                 mutable_item->direction == engine::Direction::BUY ? "买入" : "卖出",
                                 mutable_item->volume.str(), mutable_item->price.str());
      }
    }
  }
}

std::shared_ptr<engine::OrderData> MatchEngine::cancelOrder(const std::string& order_id) {
  for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ++it) {
    auto& order = *it;
    if (!order) continue;
    for (const auto& item : order->items) {
      if (!item || item->order_id != order_id) continue;
      auto mutable_item = std::const_pointer_cast<engine::OrderDataItem>(item);
      // 已成交/已撤销的子单跳过本单即可，不能整体放弃：
      // 返回 nullptr 会让调用方 continue，同批后面的可撤订单也被连累。
      if (mutable_item->status != engine::OrderStatus::PENDING) break;
      mutable_item->status = engine::OrderStatus::CANCELLED;
      // 先持有副本再 erase：erase 会销毁容器内元素，
      // 之后再访问 `*it` 的引用就是未定义行为（会导致堆损坏）。
      auto cancelled = order;
      for (const auto& item : order->items) {
        if (item) m_queue_ahead.erase(item->order_id);
      }
      m_pending_orders.erase(it);
      VLOG(1) << fmt::format("[回测撮合] 撤销挂单: {}", order_id);
      return cancelled;
    }
  }
  return nullptr;
}

void MatchEngine::onTick(engine::TickDataPtr tick) {
  if (!tick) return;

  // 记录最近盘口：调用方提交订单时不带盘口，用它兜底估算排队量。
  if (tick->order_book) m_last_book = tick->order_book;

  // 幂等：回测一帧内 onTick 会被调用两次（行情 sink 里的执行端口一次、
  // 网关自己一次），不去重会让队列推进速度翻倍。
  if (tick->timestamp_ms > 0) {
    auto& applied = m_last_applied_tick_ms[tick->symbol];
    if (applied == tick->timestamp_ms) return;
    applied = tick->timestamp_ms;
  }

  const dec_float aggressive = aggressiveVolume(tick->last_volume);

  auto it = m_pending_orders.begin();
  while (it != m_pending_orders.end()) {
    // 先拷出 shared_ptr：tryMatch 的成交回调可能间接改动挂单队列，
    // 直接把容器内元素的引用传进去会在容器重分配时失效。
    auto order = *it;
    if (tryMatch(order, tick->last_price, tick->timestamp_ms, tick->symbol, tick->order_book,
                 aggressive)) {
      it = m_pending_orders.erase(it);
    } else {
      ++it;
    }
  }
}

bool MatchEngine::tryMatch(std::shared_ptr<engine::OrderData>& order, const dec_float& price,
                           int64_t timestamp_ms, const std::string& symbol,
                           const engine::BookPtr& book,
                           const dec_float& aggressive_volume) {
  if (!order || order->items.empty()) return false;

  // 逐个子单撮合：只处理 items[0] 会让多子单订单的其余子单被静默忽略。
  bool filled_any = false;
  for (const auto& item : order->items) {
    if (!item) continue;
    if (tryMatchItem(order, item, price, timestamp_ms, symbol, book, aggressive_volume)) {
      filled_any = true;
    }
  }
  return filled_any;
}

bool MatchEngine::tryMatchItem(std::shared_ptr<engine::OrderData>& order,
                               const std::shared_ptr<const engine::OrderDataItem>& item,
                               const dec_float& price, int64_t timestamp_ms,
                               const std::string& symbol, const engine::BookPtr& book,
                               const dec_float& aggressive_volume) {
  auto mutable_item = std::const_pointer_cast<engine::OrderDataItem>(item);
  // 已终结的子单不重复撮合，避免同一笔成交被计入两次。
  if (mutable_item->status == engine::OrderStatus::FILLED ||
      mutable_item->status == engine::OrderStatus::CANCELLED ||
      mutable_item->status == engine::OrderStatus::REJECTED) {
    return false;
  }

  const bool should_fill = mutable_item->direction == engine::Direction::BUY
      // 限价买单：价格 <= 限价时成交
      ? (price <= mutable_item->price)
      // 限价卖单：价格 >= 限价时成交
      : (price >= mutable_item->price);
  if (!should_fill) return false;

  // 价格穿越只是"轮到的必要条件"：真实市场里还要把前面的排队量吃完。
  // 不吃掉队列就成交，等于假设自己的挂单永远排在第一位，会系统性高估成交率。
  if (m_fill_model.queue_model) {
    auto queue_it = m_queue_ahead.find(mutable_item->order_id);
    if (queue_it == m_queue_ahead.end()) {
      queue_it = m_queue_ahead
                     .emplace(mutable_item->order_id,
                              computeQueueAhead(book, mutable_item->direction,
                                                mutable_item->price, mutable_item->volume))
                     .first;
    }
    queue_it->second -= aggressive_volume > 0 ? aggressive_volume : dec_float(0);
    if (queue_it->second > 0) return false;  // 还没排到
    m_queue_ahead.erase(queue_it);
  }

  mutable_item->filled_volume = mutable_item->volume;
  mutable_item->status = engine::OrderStatus::FILLED;

  auto trade = std::make_shared<engine::TradeData>();
  trade->trade_id = generateId();
  trade->symbol = symbol.empty() ? (order->symbol.empty() ? mutable_item->symbol : order->symbol)
                                 : symbol;
  trade->exchange = "backtest";
  trade->timestamp_ms = timestamp_ms;
  trade->direction = mutable_item->direction;
  // 逆向选择：轮到你成交时，价格往往已经朝不利方向走过一段，
  // 用滑点把这部分成本显式记入成交价（默认 0，即不启用）。
  trade->price = applyAdverseSlippage(mutable_item->direction, mutable_item->price);
  trade->volume = mutable_item->volume;
  trade->order = order;
  m_trades.push_back(trade);

  VLOG(1) << fmt::format("[回测撮合] 限价单成交: {} {} {} @ {}",
                         mutable_item->order_id,
                           mutable_item->direction == engine::Direction::BUY ? "买入" : "卖出",
                           mutable_item->volume.str(), mutable_item->price.str());

  if (m_on_trade) {
    m_on_trade(order, trade);
  }
  return true;
}

void MatchEngine::trimTradesBefore(std::size_t cursor) {
  if (cursor == 0 || cursor > m_trades.size()) return;
  m_trades.erase(m_trades.begin(), m_trades.begin() + static_cast<std::ptrdiff_t>(cursor));
}

std::string MatchEngine::generateId() {
  return fmt::format("BT-{}", ++m_order_counter);
}

dec_float MatchEngine::computeQueueAhead(const engine::BookPtr& book,
                                         engine::Direction direction,
                                         const dec_float& limit_price,
                                         const dec_float& volume) const {
  // 无盘口时（回测 CSV 只有价格、K 线回放连成交量都没有）无法真实估算排队，
  // 退化成"半个挂单量"，既保留成交量约束，又不会让回测永久挂零。
  const dec_float fallback = volume * dec_float("0.5");
  if (!book) return fallback;

  // 排队量按挂单量封顶：盘口累计量常常是挂单量的几十上百倍，照搬会让小额
  // 做市单永远排在队尾（线上实测 16 小时零成交）。取挂单量的若干倍作为上界，
  // 既保留"要排队"的约束，又不会把小单判成永不成成交。
  const dec_float cap = volume * m_fill_model.queue_depth_factor;

  if (direction == engine::Direction::BUY) {
    if (book->bids.empty()) return fallback;
    const dec_float ahead = accumulatedBids(book, limit_price);
    // 挂单价格低于买一（做市常见）：同侧没有更优档位，用买一档量近似。
    const dec_float raw = ahead > 0 ? ahead : book->bids.front().volume;
    return raw > cap ? cap : raw;
  }
  if (book->asks.empty()) return fallback;
  const dec_float ahead = accumulatedAsks(book, limit_price);
  // 卖侧排队量取卖一档；此前误写成买一档，会把卖单的前置队列算错。
  const dec_float raw = ahead > 0 ? ahead : book->asks.front().volume;
  return raw > cap ? cap : raw;
}

dec_float MatchEngine::aggressiveVolume(const dec_float& tick_volume) const {
  const dec_float raw = tick_volume > 0 ? tick_volume : m_fill_model.default_trade_volume;
  return raw > 0 ? raw * m_fill_model.fill_ratio : dec_float(0);
}

dec_float MatchEngine::applyAdverseSlippage(engine::Direction direction,
                                            const dec_float& price) const {
  if (m_fill_model.adverse_slippage_bps <= 0) return price;
  const dec_float bps = dec_float(m_fill_model.adverse_slippage_bps) / dec_float(10000);
  // 买单成交价下移、卖单成交价上移：成交总是发生在更不利的价格上。
  return direction == engine::Direction::BUY ? price * (dec_float(1) - bps)
                                             : price * (dec_float(1) + bps);
}

}  // namespace backtest::match
