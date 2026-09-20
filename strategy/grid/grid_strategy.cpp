#include "grid_strategy.h"

#include <exception>

#include <glog/logging.h>
#include <fmt/core.h>

namespace strategy::grid {

GridStrategy::GridStrategy(const std::string& symbol,
                           dec_float upper_price,
                           dec_float lower_price,
                           int grid_count,
                           dec_float amount_per_grid)
    : m_symbol(symbol),
      m_upper_price(upper_price),
      m_lower_price(lower_price),
      m_grid_count(grid_count),
      m_amount_per_grid(amount_per_grid),
      m_total_profit(0) {
  buildGrid();
}

void GridStrategy::buildGrid() {
  if (m_grid_count <= 0 || m_upper_price <= m_lower_price || m_amount_per_grid <= 0) {
    LOG(ERROR) << "[网格策略] 参数无效，无法创建网格";
    m_grid_spacing = dec_float(0);
    m_levels.clear();
    return;
  }

  m_grid_spacing = (m_upper_price - m_lower_price) / m_grid_count;
  m_levels.clear();
  m_levels.reserve(static_cast<std::size_t>(m_grid_count) + 1);
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

void GridStrategy::initOccupancy(const dec_float& current_price) {
  int pos_count = 0;
  for (auto& level : m_levels) {
    level.has_position = (level.price < current_price);
    if (level.has_position) ++pos_count;
  }
  LOG(INFO) << fmt::format(
      "[网格策略] 初始化完成，当前价格: {}, 持仓层级: {}/{}, 等待触发: {}",
      current_price.str(), pos_count, m_levels.size(),
      m_levels.size() - static_cast<std::size_t>(pos_count));
}

asio::awaitable<void> GridStrategy::run() {
  LOG(INFO) << fmt::format("[网格策略] 启动，交易对: {}", m_symbol);
  LOG(INFO) << "[网格策略] 已接入运行时，等待行情快照初始化网格...";
  co_return;
}

void GridStrategy::onMarket(const core::domain::MarketSnapshot& snapshot) {
  if (snapshot.symbol != m_symbol || snapshot.last_price <= 0) return;
  const dec_float current_price = snapshot.last_price;
  if (current_price < m_lower_price || current_price > m_upper_price) {
    LOG(WARNING) << fmt::format(
        "[网格策略] 当前价格 {} 超出网格范围 [{}, {}]",
        current_price.str(), m_lower_price.str(), m_upper_price.str());
    return;
  }
  if (!m_initialized) {
    initOccupancy(current_price);
    m_initialized = true;
    LOG(INFO) << fmt::format("[网格策略] 网格已激活，首个价格: {}", current_price.str());
  }
  submitPlan(current_price);
}

void GridStrategy::submitPlan(const dec_float& current_price) {
  const auto context = runtime_context();
  if (!context) {
    LOG(WARNING) << "[网格策略] 未注入策略运行时上下文，无法提交订单计划";
    return;
  }

  core::domain::OrderPlan plan;
  plan.plan_id = fmt::format("grid:{}", m_symbol);
  plan.idempotency_key.value = plan.plan_id;
  plan.strategy_id = plan.plan_id;
  plan.symbol = m_symbol;
  plan.replace_policy = core::domain::ReplacePolicy::CANCEL_MISSING;

  for (int i = 0; i < static_cast<int>(m_levels.size()); ++i) {
    const auto& level = m_levels[static_cast<std::size_t>(i)];
    core::domain::OrderIntent intent;
    intent.symbol = m_symbol;
    intent.type = core::domain::OrderType::LIMIT;
    intent.quantity = m_amount_per_grid;
    intent.level = i;
    if (level.has_position) {
      if (i + 1 >= static_cast<int>(m_levels.size())) continue;
      if (current_price < level.price + m_grid_spacing) continue;
      intent.intent_id = fmt::format("sell-{}", i);
      intent.side = core::domain::Side::SELL;
      intent.price = level.price + m_grid_spacing;
      intent.reduce_only = true;
    } else {
      if (current_price > level.price) continue;
      intent.intent_id = fmt::format("buy-{}", i);
      intent.side = core::domain::Side::BUY;
      intent.price = level.price;
    }
    plan.intents.push_back(std::move(intent));
  }

  const auto result = context->submit(plan);
  if (!result.accepted) {
    LOG(WARNING) << fmt::format("[网格策略] 计划未提交: {}", result.error.message);
  }
}

void GridStrategy::onExecution(const core::domain::ExecutionReport& report) {
  if (report.symbol != m_symbol) return;
  if (report.type != core::domain::ExecutionEventType::FILL) return;
  const int level_idx = levelFromIntent(report.intent_id);
  if (level_idx < 0 || level_idx >= static_cast<int>(m_levels.size())) return;

  auto& level = m_levels[static_cast<std::size_t>(level_idx)];
  ++m_total_trades;
  if (report.side == core::domain::Side::BUY) {
    level.has_position = true;
    LOG(INFO) << fmt::format(
        "[网格策略] 买单成交: 价格={}, 层级={}, 累计交易: {}",
        report.price.str(), level_idx, m_total_trades);
    return;
  }
  level.has_position = false;
  const dec_float grid_profit = m_grid_spacing * m_amount_per_grid;
  m_total_profit += grid_profit;
  LOG(INFO) << fmt::format(
      "[网格策略] 卖单成交: 价格={}, 层级={}, 本格利润: {}, 累计利润: {}, 累计交易: {}",
      report.price.str(), level_idx, grid_profit.str(), m_total_profit.str(),
      m_total_trades);
}

int GridStrategy::levelFromIntent(const std::string& intent_id) {
  const auto pos = intent_id.find_last_of('-');
  if (pos == std::string::npos || pos + 1 >= intent_id.size()) return -1;
  try {
    return std::stoi(intent_id.substr(pos + 1));
  } catch (const std::exception&) {
    return -1;
  }
}

}  // namespace strategy::grid
