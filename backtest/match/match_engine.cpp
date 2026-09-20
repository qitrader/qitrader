#include "match_engine.h"

#include <glog/logging.h>

namespace backtest::match {

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
      // 限价单：先尝试即时成交，否则加入挂单队列
      if (!tryMatch(order, current_price, order->timestamp_ms, order->symbol)) {
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
      if (mutable_item->status != engine::OrderStatus::PENDING) return nullptr;
      mutable_item->status = engine::OrderStatus::CANCELLED;
      // 先持有副本再 erase：erase 会销毁容器内元素，
      // 之后再访问 `*it` 的引用就是未定义行为（会导致堆损坏）。
      auto cancelled = order;
      m_pending_orders.erase(it);
      VLOG(1) << fmt::format("[回测撮合] 撤销挂单: {}", order_id);
      return cancelled;
    }
  }
  return nullptr;
}

void MatchEngine::onTick(engine::TickDataPtr tick) {
  if (!tick) return;

  auto it = m_pending_orders.begin();
  while (it != m_pending_orders.end()) {
    // 先拷出 shared_ptr：tryMatch 的成交回调可能间接改动挂单队列，
    // 直接把容器内元素的引用传进去会在容器重分配时失效。
    auto order = *it;
    if (tryMatch(order, tick->last_price, tick->timestamp_ms, tick->symbol)) {
      it = m_pending_orders.erase(it);
    } else {
      ++it;
    }
  }
}

bool MatchEngine::tryMatch(std::shared_ptr<engine::OrderData>& order, const dec_float& price,
                           int64_t timestamp_ms, const std::string& symbol) {
  if (!order || order->items.empty()) return false;

  auto& item = order->items[0];
  auto mutable_item = std::const_pointer_cast<engine::OrderDataItem>(item);

  bool should_fill = false;

  if (mutable_item->direction == engine::Direction::BUY) {
    // 限价买单：价格 <= 限价时成交
    should_fill = (price <= mutable_item->price);
  } else {
    // 限价卖单：价格 >= 限价时成交
    should_fill = (price >= mutable_item->price);
  }

  if (should_fill) {
    mutable_item->filled_volume = mutable_item->volume;
    mutable_item->status = engine::OrderStatus::FILLED;

    auto trade = std::make_shared<engine::TradeData>();
    trade->trade_id = generateId();
    trade->symbol = order->symbol.empty() ? mutable_item->symbol : order->symbol;
    trade->exchange = "backtest";
    trade->timestamp_ms = timestamp_ms;
    trade->direction = mutable_item->direction;
    trade->price = mutable_item->price;
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

  return false;
}

std::string MatchEngine::generateId() {
  return fmt::format("BT-{}", ++m_order_counter);
}

}  // namespace backtest::match
