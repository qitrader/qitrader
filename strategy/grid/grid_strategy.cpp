#include "grid_strategy.h"

#include <glog/logging.h>
#include <fmt/core.h>
#include <chrono>

namespace strategy::grid {

GridStrategy::GridStrategy(engine::EnginePtr engine,
                           const std::string& symbol,
                           dec_float upper_price,
                           dec_float lower_price,
                           int grid_count,
                           dec_float amount_per_grid)
    : base::Strategy(engine),
      m_symbol(symbol),
      m_upper_price(upper_price),
      m_lower_price(lower_price),
      m_grid_count(grid_count),
      m_amount_per_grid(amount_per_grid),
      m_total_profit(0) {
  build_grid();
}

GridStrategy::~GridStrategy() {}

void GridStrategy::build_grid() {
  if (m_grid_count <= 0 || m_upper_price <= m_lower_price || m_amount_per_grid <= 0) {
    LOG(ERROR) << "[网格策略] 参数无效，无法创建网格";
    m_grid_spacing = dec_float(0);
    m_levels.clear();
    return;
  }

  // 计算网格间距 = (上界 - 下界) / 网格数量
  m_grid_spacing = (m_upper_price - m_lower_price) / m_grid_count;

  m_levels.clear();
  m_levels.reserve(m_grid_count + 1);

  // 生成从低到高的网格价格层级
  // level[0] = lower_price, level[N] = upper_price
  for (int i = 0; i <= m_grid_count; ++i) {
    GridLevel level;
    level.price = m_lower_price + m_grid_spacing * i;
    level.has_position = false;
    m_levels.push_back(level);
  }

  LOG(INFO) << fmt::format(
      "[网格策略] 交易对: {}, 区间: [{}, {}], 网格数: {}, 间距: {}, 每格数量: {}",
      m_symbol, m_lower_price.str(), m_upper_price.str(),
      m_grid_count, m_grid_spacing.str(), m_amount_per_grid.str());
}

void GridStrategy::init_grid_positions(dec_float current_price) {
  // 假设当前价格以下的层级都"已持仓"（模拟已买入）
  // 当前价格以上的层级"未持仓"（等待买入或已卖出）
  for (size_t i = 0; i < m_levels.size(); ++i) {
    m_levels[i].has_position = (m_levels[i].price < current_price);
  }

  int pos_count = 0;
  for (auto& lv : m_levels) {
    if (lv.has_position) pos_count++;
  }

  LOG(INFO) << fmt::format(
      "[网格策略] 初始化完成，当前价格: {}, 持仓层级: {}/{}, 等待触发: {}",
      current_price.str(), pos_count, m_levels.size(),
      m_levels.size() - pos_count);
}

asio::awaitable<void> GridStrategy::run() {
  LOG(INFO) << fmt::format("[网格策略] 启动，交易对: {}", m_symbol);

  // 查询账户和持仓信息
  co_await on_request_account();
  co_await on_request_position();

  // 订阅 Tick 数据，网格逻辑靠 Tick 驱动
  co_await on_subscribe_tick(m_symbol);

  LOG(INFO) << "[网格策略] 已订阅行情，等待第一个 Tick 初始化网格...";
  co_return;
}

asio::awaitable<void> GridStrategy::recv_account(engine::AccountDataPtr account) {
  if (!account) co_return;
  // Runtime 模式下账户状态以统一账本快照为准，Legacy 事件由 LegacyLedgerAdapter 单向同步。
  LOG(INFO) << fmt::format("[网格策略] 账户余额: {}", account->balance.str());
  co_return;
}

asio::awaitable<void> GridStrategy::recv_position(engine::PositionDataPtr position) {
  if (!position) co_return;
  // Runtime 模式下持仓状态以统一账本快照为准，策略只读不写。
  LOG(INFO) << fmt::format("[网格策略] 持仓数: {}", position->items.size());
  for (auto& item : position->items) {
    if (!item) continue;
    LOG(INFO) << fmt::format("  {} {} @ {} 方向: {}",
        item->symbol, item->volume.str(), item->price.str(),
        item->direction == engine::Direction::BUY ? "多" : "空");
  }
  co_return;
}

asio::awaitable<void> GridStrategy::recv_book(engine::BookPtr book) {
  // 网格策略主要靠 Tick 驱动，订单簿数据仅做参考
  co_return;
}

asio::awaitable<void> GridStrategy::recv_tick(engine::TickDataPtr ticker) {
  if (ticker->symbol != m_symbol) {
    co_return;
  }

  dec_float current_price = ticker->last_price;

  // Runtime 模式下的市场快照由 MarketDataFeed 驱动，策略只读不写。

  // 价格超出网格范围，仅输出警告
  if (current_price < m_lower_price || current_price > m_upper_price) {
    LOG(WARNING) << fmt::format(
        "[网格策略] 当前价格 {} 超出网格范围 [{}, {}]",
        current_price.str(), m_lower_price.str(), m_upper_price.str());
    co_return;
  }

  // 收到第一个 Tick 时初始化网格持仓状态
  if (!m_initialized) {
    init_grid_positions(current_price);
    m_initialized = true;
    LOG(INFO) << fmt::format("[网格策略] 网格已激活，首个价格: {}", current_price.str());
  }

  co_await check_grid(current_price);
  co_return;
}

asio::awaitable<void> GridStrategy::recv_bar(engine::BarDataPtr bar) {
  if (!bar || bar->symbol != m_symbol) {
    co_return;
  }
  co_return;
}

asio::awaitable<void> GridStrategy::check_grid(dec_float current_price) {
  // 遍历所有网格层级，检查是否需要触发交易
  for (int i = 0; i < static_cast<int>(m_levels.size()); ++i) {
    auto& level = m_levels[i];

    if (level.has_position) {
      // 已持仓层级：检查是否应该卖出
      // 当价格上穿到该层级上方一格时，触发卖出
      if (i + 1 < static_cast<int>(m_levels.size())) {
        dec_float sell_trigger = level.price + m_grid_spacing;
        if (current_price >= sell_trigger && level.sell_order_id.empty()) {
          co_await place_sell_order(i);
        }
      }
    } else {
      // 未持仓层级：检查是否应该买入
      // 当价格下穿到该层级时，触发买入
      if (current_price <= level.price && level.buy_order_id.empty()) {
        co_await place_buy_order(i);
      }
    }
  }
}

asio::awaitable<void> GridStrategy::place_buy_order(int level_index) {
  auto& level = m_levels[level_index];
  if (level.has_position) co_return;  // 已持仓，不重复买

  const auto context = runtime_context();
  if (!context) {
    LOG(WARNING) << "[网格策略] 未注入策略运行时上下文，无法提交订单计划";
    co_return;
  }

  core::domain::OrderPlan plan;
  plan.plan_id = generate_order_id();
  plan.idempotency_key.value = plan.plan_id;
  plan.strategy_id = fmt::format("grid:{}", m_symbol);
  plan.symbol = m_symbol;
  plan.replace_policy = core::domain::ReplacePolicy::KEEP_EXISTING;
  core::domain::OrderIntent intent;
  intent.intent_id = fmt::format("buy-{}", level_index);
  intent.symbol = m_symbol;
  intent.side = core::domain::Side::BUY;
  intent.type = core::domain::OrderType::LIMIT;
  intent.price = level.price;
  intent.quantity = m_amount_per_grid;
  plan.intents.push_back(std::move(intent));

  const auto result = co_await context->submitAsync(plan);
  if (!result.accepted) {
    LOG(WARNING) << fmt::format("[网格策略] 买单未提交: {}", result.error.message);
    co_return;
  }
  const std::string runtime_order_id = fmt::format("{}:buy-{}", plan.plan_id, level_index);
  level.buy_order_id = runtime_order_id;
  m_order_to_level[runtime_order_id] = level_index;
  LOG(INFO) << fmt::format(
      "[网格策略] 挂买单 #{}: 价格={}, 数量={}, 层级={}",
      runtime_order_id, level.price.str(), m_amount_per_grid.str(), level_index);
}

asio::awaitable<void> GridStrategy::place_sell_order(int level_index) {
  auto& level = m_levels[level_index];
  if (!level.has_position) co_return;  // 无持仓，不能卖

  dec_float sell_price = level.price + m_grid_spacing;

  const auto context = runtime_context();
  if (!context) {
    LOG(WARNING) << "[网格策略] 未注入策略运行时上下文，无法提交订单计划";
    co_return;
  }

  core::domain::OrderPlan plan;
  plan.plan_id = generate_order_id();
  plan.idempotency_key.value = plan.plan_id;
  plan.strategy_id = fmt::format("grid:{}", m_symbol);
  plan.symbol = m_symbol;
  plan.replace_policy = core::domain::ReplacePolicy::KEEP_EXISTING;
  core::domain::OrderIntent intent;
  intent.intent_id = fmt::format("sell-{}", level_index);
  intent.symbol = m_symbol;
  intent.side = core::domain::Side::SELL;
  intent.type = core::domain::OrderType::LIMIT;
  intent.price = sell_price;
  intent.quantity = m_amount_per_grid;
  // 网格卖单是平掉该层买单建立的仓位，声明只减仓避免意外开空。
  intent.reduce_only = true;
  plan.intents.push_back(std::move(intent));

  const auto result = co_await context->submitAsync(plan);
  if (!result.accepted) {
    LOG(WARNING) << fmt::format("[网格策略] 卖单未提交: {}", result.error.message);
    co_return;
  }
  const std::string runtime_order_id = fmt::format("{}:sell-{}", plan.plan_id, level_index);
  level.sell_order_id = runtime_order_id;
  m_order_to_level[runtime_order_id] = level_index;
  LOG(INFO) << fmt::format(
      "[网格策略] 挂卖单 #{}: 价格={}, 数量={}, 层级={}",
      runtime_order_id, sell_price.str(), m_amount_per_grid.str(), level_index);
}

asio::awaitable<void> GridStrategy::recv_order(engine::OrderDataPtr order) {
  for (auto& item : order->items) {
    auto it = m_order_to_level.find(item->order_id);
    if (it == m_order_to_level.end()) continue;

    int level_idx = it->second;
    auto& level = m_levels[level_idx];

    if (item->status == engine::OrderStatus::FILLED) {
      m_total_trades++;

      if (item->direction == engine::Direction::BUY) {
        // 买单成交 → 标记该层持仓，清除买单 ID
        level.has_position = true;
        level.buy_order_id.clear();
        m_order_to_level.erase(it);

        LOG(INFO) << fmt::format(
            "[网格策略] 买单成交 #{}: 价格={}, 层级={}, 累计交易: {}",
            item->order_id, item->price.str(), level_idx, m_total_trades);

      } else if (item->direction == engine::Direction::SELL) {
        // 卖单成交 → 清除持仓，清除卖单 ID，计算利润
        level.has_position = false;
        level.sell_order_id.clear();
        m_order_to_level.erase(it);

        // 单格利润 = 网格间距 × 数量
        dec_float grid_profit = m_grid_spacing * m_amount_per_grid;
        m_total_profit += grid_profit;

        LOG(INFO) << fmt::format(
            "[网格策略] 卖单成交 #{}: 价格={}, 层级={}, 本格利润: {}, 累计利润: {}, 累计交易: {}",
            item->order_id, item->price.str(), level_idx,
            grid_profit.str(), m_total_profit.str(), m_total_trades);
      }
    } else if (item->status == engine::OrderStatus::REJECTED ||
               item->status == engine::OrderStatus::CANCELLED) {
      // 订单被拒绝或取消，清除状态以便重试
      if (item->direction == engine::Direction::BUY) {
        level.buy_order_id.clear();
      } else {
        level.sell_order_id.clear();
      }
      m_order_to_level.erase(it);

      LOG(WARNING) << fmt::format(
          "[网格策略] 订单 #{} 被拒绝/取消，层级={}", item->order_id, level_idx);
    }
  }
  co_return;
}

std::string GridStrategy::generate_order_id() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count();
  return fmt::format("GRID-{}-{}", ms, ++m_order_seq);
}

}  // namespace strategy::grid
