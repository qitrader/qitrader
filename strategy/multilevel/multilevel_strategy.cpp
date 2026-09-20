#include "multilevel_strategy.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <utility>

#include <fmt/core.h>
#include <glog/logging.h>

namespace strategy::multilevel {

namespace {

double toDouble(const dec_float& value) {
  return value.convert_to<double>();
}

dec_float toDecimal(double value) {
  return dec_float(std::to_string(value));
}

}  // namespace

MultiLevelMarketMakingStrategy::MultiLevelMarketMakingStrategy(
    engine::EnginePtr engine, MultiLevelConfig config)
    : base::Strategy(engine),
      m_config(std::move(config)),
      m_policy(6 + 2 * static_cast<std::size_t>(std::max(1, m_config.levels)),
               3 + 2 * static_cast<std::size_t>(std::max(1, m_config.levels)),
               m_config.learning_rate, m_config.exploration) {
  m_config.levels = std::max(1, m_config.levels);
  m_config.order_budget = std::max(1, m_config.order_budget);
  m_config.order_size = std::max(0.00000001, m_config.order_size);
  m_config.inventory_limit = std::max(m_config.order_size, m_config.inventory_limit);
  m_config.decision_interval_ms = std::max<int64_t>(0, m_config.decision_interval_ms);
  loadModel();
}

bool MultiLevelMarketMakingStrategy::loadModel() {
  if (m_config.model_path.empty()) return false;

  std::ifstream probe(m_config.model_path);
  if (!probe.good()) {
    LOG(INFO) << fmt::format("[多层级做市] 未找到模型 {}，从初始权重开始学习",
                             m_config.model_path);
    return false;
  }

  const bool ok = m_policy.load(m_config.model_path);
  if (ok) {
    LOG(INFO) << fmt::format("[多层级做市] 已加载模型: {}", m_config.model_path);
  } else {
    // 维度不匹配通常意味着 levels 配置变了，旧权重已无意义，退回初始权重。
    LOG(WARNING) << fmt::format("[多层级做市] 模型 {} 与当前配置不匹配，改用初始权重",
                               m_config.model_path);
  }
  return ok;
}

bool MultiLevelMarketMakingStrategy::saveModel() const {
  if (m_config.model_path.empty()) return false;

  const bool ok = m_policy.save(m_config.model_path);
  if (ok) {
    LOG(INFO) << fmt::format("[多层级做市] 已保存模型: {}", m_config.model_path);
  } else {
    LOG(ERROR) << fmt::format("[多层级做市] 保存模型失败: {}", m_config.model_path);
  }
  return ok;
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::shutdown() {
  if (m_train_steps > 0) {
    LOG(INFO) << fmt::format("[多层级做市] 训练汇总: 共 {} 步, 平均奖励 {:.6f}",
                             m_train_steps, m_train_reward_sum / m_train_steps);
  }
  saveModel();
  co_return;
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::run() {
  LOG(INFO) << fmt::format(
      "[多层级做市] 启动: symbol={}, levels={}, budget={}, size={}, inventory_limit={}, interval={}ms",
      m_config.symbol, m_config.levels, m_config.order_budget, m_config.order_size,
      m_config.inventory_limit, m_config.decision_interval_ms);
  co_await on_request_account();
  co_await on_request_position();
  co_await on_subscribe_book(m_config.symbol);
  co_await on_subscribe_tick(m_config.symbol);
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::recv_account(engine::AccountDataPtr account) {
  if (!account) co_return;
  // 资金状态以统一账本为准，Legacy 账户事件由 LegacyLedgerAdapter 单向同步。
  if (m_previous_cash == 0.0) m_previous_cash = effectiveCash();
  co_return;
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::recv_position(
    engine::PositionDataPtr position) {
  if (!position) co_return;
  // 持仓状态以统一账本为准，Legacy 持仓事件由 LegacyLedgerAdapter 单向同步。
  co_return;
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::recv_book(engine::BookPtr book) {
  if (!book || book->symbol != m_config.symbol) co_return;
  if (book->bids.empty() || book->asks.empty()) co_return;
  m_book = book;
  const double bid = toDouble(book->bids.front().price);
  const double ask = toDouble(book->asks.front().price);
  if (bid > 0.0 && ask >= bid) {
    m_tick_size = std::max(ask - bid, bid * 0.0001);
  }
  co_return;
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::recv_tick(engine::TickDataPtr ticker) {
  if (!ticker || ticker->symbol != m_config.symbol || ticker->last_price <= 0) co_return;

  m_last_price = toDouble(ticker->last_price);
  if (ticker->order_book && !ticker->order_book->bids.empty() &&
      !ticker->order_book->asks.empty()) {
    m_book = ticker->order_book;
  } else {
    m_book = makeSyntheticBook(ticker->last_price, ticker->timestamp_ms);
  }
  if (m_book && !m_book->bids.empty() && !m_book->asks.empty()) {
    const double bid = toDouble(m_book->bids.front().price);
    const double ask = toDouble(m_book->asks.front().price);
    if (ask >= bid && bid > 0.0) m_tick_size = std::max(ask - bid, bid * 0.0001);
  }
  // Runtime 模式下的市场快照由 MarketDataFeed 驱动，策略只读不写。

  // 必须用行情时间戳推进策略时钟，否则 m_last_decision_timestamp 只会自增，
  // 决策间隔判断永远成立，策略会退化成每个 tick 都下单。
  m_current_timestamp = ticker->timestamp_ms;

  const bool first_decision = m_last_decision_timestamp == 0;
  const bool interval_elapsed = first_decision ||
      m_config.decision_interval_ms == 0 ||
      m_current_timestamp - m_last_decision_timestamp >= m_config.decision_interval_ms;
  if (interval_elapsed) {
    co_await reconfigureOrders(makeObservation());
  }
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::recv_bar(engine::BarDataPtr bar) {
  if (!bar || bar->symbol != m_config.symbol || bar->close_price <= 0) co_return;
  auto tick = std::make_shared<engine::TickData>();
  tick->symbol = bar->symbol;
  tick->exchange = bar->exchange;
  tick->timestamp_ms = bar->timestamp_ms;
  tick->last_price = bar->close_price;
  co_await recv_tick(tick);
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::recv_order(engine::OrderDataPtr order) {
  if (!order) co_return;
  // 成交已由运行时转为执行回报并记入统一账本，订单表由 OrderManager 维护，
  // 策略不再自行记账，这里只保留成交日志便于排查。
  for (const auto& item : order->items) {
    if (!item || item->symbol != m_config.symbol) continue;
    if (item->status != engine::OrderStatus::FILLED &&
        item->status != engine::OrderStatus::PARTIAL_FILLED) {
      continue;
    }
    // 成交明细只在 --v=1 输出；累计笔数见模拟交易摘要。
    VLOG(1) << fmt::format("[多层级做市] 成交 #{}: {} {} @ {}", item->order_id,
                           item->direction == engine::Direction::BUY ? "买入" : "卖出",
                           item->filled_volume.str(), item->price.str());
  }
  co_return;
}

double MultiLevelMarketMakingStrategy::currentMidPrice() const {
  if (m_book && !m_book->bids.empty() && !m_book->asks.empty()) {
    const double bid = toDouble(m_book->bids.front().price);
    const double ask = toDouble(m_book->asks.front().price);
    if (bid > 0.0 && ask >= bid) return (bid + ask) / 2.0;
  }
  return m_last_price;
}

double MultiLevelMarketMakingStrategy::tickSize() const {
  if (m_tick_size > 0.0) return m_tick_size;
  return std::max(0.01, currentMidPrice() * 0.0001);
}

double MultiLevelMarketMakingStrategy::effectiveCash() const {
  const auto context = runtime_context();
  if (!context) return 0.0;
  const auto portfolio = context->portfolio();
  return portfolio ? toDouble(portfolio->cash) : 0.0;
}

double MultiLevelMarketMakingStrategy::effectiveInventory() const {
  const auto context = runtime_context();
  if (!context) return 0.0;
  const auto portfolio = context->portfolio();
  if (!portfolio) return 0.0;
  for (const auto& position : portfolio->positions) {
    if (position.symbol != m_config.symbol) continue;
    return position.side == core::domain::Side::BUY
        ? toDouble(position.quantity) : -toDouble(position.quantity);
  }
  return 0.0;
}

double MultiLevelMarketMakingStrategy::availableInventory() const {
  const auto context = runtime_context();
  if (!context) return 0.0;
  const auto portfolio = context->portfolio();
  if (!portfolio) return 0.0;
  for (const auto& position : portfolio->positions) {
    if (position.symbol != m_config.symbol) continue;
    const double net = position.side == core::domain::Side::BUY
        ? toDouble(position.quantity) : -toDouble(position.quantity);
    // 只有多头方向的库存才需要扣除冻结；空头方向直接返回净值。
    if (net <= 0.0) return net;
    return std::max(0.0, net - toDouble(position.frozen_quantity));
  }
  return 0.0;
}

engine::BookPtr MultiLevelMarketMakingStrategy::makeSyntheticBook(
    const dec_float& price, int64_t timestamp_ms) const {
  const double mid = toDouble(price);
  const double step = std::max(0.01, mid * 0.0001);
  auto book = std::make_shared<engine::Book>();
  book->symbol = m_config.symbol;
  book->exchange = "synthetic";
  book->timestamp_ms = timestamp_ms;
  for (int level = 0; level < m_config.levels; ++level) {
    engine::BookItem bid;
    bid.symbol = m_config.symbol;
    bid.price = toDecimal(mid - step * (level + 1));
    bid.volume = dec_float("10");
    book->bids.push_back(bid);

    engine::BookItem ask;
    ask.symbol = m_config.symbol;
    ask.price = toDecimal(mid + step * (level + 1));
    ask.volume = dec_float("10");
    book->asks.push_back(ask);
  }
  return book;
}

std::vector<double> MultiLevelMarketMakingStrategy::makeObservation() const {
  const std::size_t order_feature_size = 6 + 2 * static_cast<std::size_t>(m_config.levels);
  std::vector<double> observation(order_feature_size, 0.0);
  const double mid = std::max(currentMidPrice(), 1e-12);
  double bid_depth = 0.0;
  double ask_depth = 0.0;
  double spread = tickSize();
  double imbalance = 0.0;

  if (m_book && !m_book->bids.empty() && !m_book->asks.empty()) {
    const double bid = toDouble(m_book->bids.front().price);
    const double ask = toDouble(m_book->asks.front().price);
    spread = std::max(0.0, ask - bid);
    const std::size_t bid_count = std::min<std::size_t>(m_book->bids.size(), m_config.levels);
    const std::size_t ask_count = std::min<std::size_t>(m_book->asks.size(), m_config.levels);
    for (std::size_t i = 0; i < bid_count; ++i) bid_depth += toDouble(m_book->bids[i].volume);
    for (std::size_t i = 0; i < ask_count; ++i) ask_depth += toDouble(m_book->asks[i].volume);
    const double total_depth = bid_depth + ask_depth;
    if (total_depth > 0.0) imbalance = (bid_depth - ask_depth) / total_depth;
  }

  const double previous_mid = m_feature_mid > 0.0 ? m_feature_mid : mid;
  const double inventory = effectiveInventory();
  observation[0] = std::clamp(inventory / std::max(m_config.inventory_limit, 1e-12), -1.0, 1.0);
  observation[1] = std::clamp(spread / mid * 10000.0, 0.0, 10.0) / 10.0;
  observation[2] = std::clamp(imbalance, -1.0, 1.0);
  observation[3] = std::clamp((mid - previous_mid) / previous_mid * 100.0, -1.0, 1.0);
  // 深度特征的量纲必须匹配盘口真实深度：原先除以 order_budget*order_size
  // （levels=2、size=0.01 时只有 0.2 ETH），而 ETH 单档深度常在 1~10 ETH，
  // 比值 5~50 全部被 clamp 到上限，两个维度恒为 1.0，退化为常量特征。
  // 改为按 lot 计数的对数压缩：既不会饱和，又对深度变化保持单调。
  const double depth_scale = std::log1p(1000.0);
  observation[4] = std::clamp(
      std::log1p(bid_depth / std::max(m_config.order_size, 1e-12)) / depth_scale, 0.0, 1.0);
  observation[5] = std::clamp(
      std::log1p(ask_depth / std::max(m_config.order_size, 1e-12)) / depth_scale, 0.0, 1.0);

  // 自身挂单特征来自运行时的活动订单表，策略不再自行维护订单状态。
  if (const auto context = runtime_context()) {
    for (const auto& active : context->activeOrders()) {
      if (active.intent.level < 0 || active.intent.level >= m_config.levels) continue;
      const std::size_t offset = 6 + static_cast<std::size_t>(active.intent.level) * 2;
      const bool is_buy = active.intent.side == core::domain::Side::BUY;
      observation[offset + (is_buy ? 0 : 1)] +=
          toDouble(active.intent.quantity) /
          std::max(1.0, m_config.order_budget * m_config.order_size);
    }
  }
  for (std::size_t i = 6; i < observation.size(); ++i) observation[i] = std::clamp(observation[i], 0.0, 1.0);
  return observation;
}

void MultiLevelMarketMakingStrategy::updateTransition(const std::vector<double>& observation) {
  const double mid = std::max(currentMidPrice(), 1e-12);
  if (m_has_transition) {
    const double cash = effectiveCash();
    const double inventory = effectiveInventory();
    const double cash_change = cash - m_previous_cash;
    const double inventory_potential = inventory * mid - m_previous_inventory * m_previous_mid;
    const double inventory_cost = m_config.inventory_penalty *
        std::abs(inventory / std::max(m_config.inventory_limit, 1e-12));
    const double reward = cash_change + inventory_potential - inventory_cost;
    m_policy.update(m_previous_observation, m_previous_action, reward,
                    observation, false);

    // 累计训练统计：平均奖励是否随时间上升，是判断策略在学的直接依据。
    ++m_train_steps;
    m_train_reward_sum += reward;
    m_train_reward_window += reward;
    // 窗口取 50：真实盘口下决策间隔通常是数十秒，200 步要几小时才输出一次，
    // 不利于观察收敛趋势。
    if (++m_train_window_count >= 50) {
      LOG(INFO) << fmt::format(
          "[多层级做市] 训练进度: 步数 {}, 近 {} 步平均奖励 {:.6f}, 累计平均 {:.6f}",
          m_train_steps, m_train_window_count,
          m_train_reward_window / m_train_window_count,
          m_train_reward_sum / m_train_steps);
      m_train_reward_window = 0.0;
      m_train_window_count = 0;
    }

    // 周期性落盘：模型若只在 shutdown() 保存，进程被强杀或机器重启时
    // 本次运行的全部在线学习成果都会丢失，重启后又要从旧权重开始。
    if (m_config.model_save_interval_steps > 0 &&
        m_train_steps % static_cast<std::size_t>(m_config.model_save_interval_steps) == 0) {
      saveModel();
    }
  }
  m_previous_observation = observation;
  m_previous_cash = effectiveCash();
  m_previous_inventory = effectiveInventory();
  m_previous_mid = mid;
  m_feature_mid = mid;
  if (auto context = runtime_context()) {
    core::domain::StrategyStateSnapshot state;
    state.timestamp_ms = m_current_timestamp;
    state.features = observation;
    context->updateState(std::move(state));
  }
  m_has_transition = true;
}

std::vector<int> MultiLevelMarketMakingStrategy::allocateLots(
    const std::vector<double>& action) const {
  std::vector<double> shares = action;
  if (shares.size() != static_cast<std::size_t>(3 + 2 * m_config.levels)) return {};

  const int buy_limit_start = 2;
  const int sell_market_index = 2 + m_config.levels;
  const int sell_limit_start = sell_market_index + 1;
  const double inventory = effectiveInventory();
  // 卖出只能用尚未被挂单冻结的库存，否则会挂出被风控拒绝的卖单。
  const double available = availableInventory();
  // 市价单按 taker 计费且立即成交，在窄价差市场里不可能覆盖成本，
  // 关闭后预算只会在限价档之间分配，动作 0（不交易）也更容易学到权重。
  if (!m_config.allow_market_orders) {
    shares[1] = 0.0;
    shares[sell_market_index] = 0.0;
  }
  if (inventory >= m_config.inventory_limit) {
    shares[1] = 0.0;
    for (int level = 0; level < m_config.levels; ++level) {
      shares[buy_limit_start + level] = 0.0;
    }
  }
  if (available <= 0.0) {
    shares[sell_market_index] = 0.0;
    for (int level = 0; level < m_config.levels; ++level) {
      shares[sell_limit_start + level] = 0.0;
    }
  }

  // 买入容量必须同时受库存上限和可用现金约束：只按库存上限计算会挂出
  // 远超资金的买单，把统一账本的现金透支成巨额负数，净值与训练奖励随之失真。
  // 这里与卖出侧使用 availableInventory() 的约束保持对称。
  // 买单按卖一价成交、且要额外付手续费，实际成本高于中间价，
  // 因此按中间价估算时留一点缓冲，避免现金被小幅透支。
  const double mid_price = std::max(currentMidPrice(), 1e-12);
  const double affordable = std::max(0.0, effectiveCash()) / (mid_price * 1.002);
  const double buy_capacity =
      std::max(0.0, std::min(m_config.inventory_limit - inventory, affordable));
  const double sell_capacity = std::max(0.0, available);
  const int max_buy_lots = static_cast<int>(std::floor(buy_capacity / m_config.order_size + 1e-9));
  const int max_sell_lots = static_cast<int>(std::floor(sell_capacity / m_config.order_size + 1e-9));
  if (max_buy_lots <= 0) {
    shares[1] = 0.0;
    for (int level = 0; level < m_config.levels; ++level) {
      shares[buy_limit_start + level] = 0.0;
    }
  }
  if (max_sell_lots <= 0) {
    shares[sell_market_index] = 0.0;
    for (int level = 0; level < m_config.levels; ++level) {
      shares[sell_limit_start + level] = 0.0;
    }
  }

  const double total = std::accumulate(shares.begin(), shares.end(), 0.0);
  std::vector<int> lots(shares.size(), 0);
  if (total <= 0.0) return lots;
  for (double& share : shares) share /= total;

  std::vector<double> fractions(shares.size(), 0.0);
  int allocated = 0;
  for (std::size_t i = 0; i < shares.size(); ++i) {
    const double raw = shares[i] * m_config.order_budget;
    lots[i] = static_cast<int>(std::floor(raw));
    fractions[i] = raw - lots[i];
    allocated += lots[i];
  }
  std::vector<std::size_t> order(shares.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&fractions](std::size_t lhs, std::size_t rhs) {
    return fractions[lhs] > fractions[rhs];
  });
  // 最大余数法补齐到预算。必须跳过被清零的动作：这些动作因库存/资金约束
  // 被置零，若仍参与余量分配，就会挂出"无库存却卖""无现金却买"的无效单，
  // 在回测里表现为大量 no available position / cash 诊断，在实盘里直接变成拒单。
  // fractions 已按降序排列，遇到第一个被禁用的动作即可停止。
  for (int i = allocated; i < m_config.order_budget; ++i) {
    const int offset = i - allocated;
    if (offset >= static_cast<int>(order.size())) break;
    const std::size_t target = order[static_cast<std::size_t>(offset)];
    if (shares[target] <= 0.0) break;
    ++lots[target];
  }

  int buy_lots = lots[1];
  int sell_lots = lots[sell_market_index];
  for (int level = 0; level < m_config.levels; ++level) {
    buy_lots += lots[buy_limit_start + level];
    sell_lots += lots[sell_limit_start + level];
  }
  // 削减顺序：先砍最远的限价档，最后才砍市价单。市价单是 taker 且立即成交，
  // 成本最高，因此放在削减链末端；但必须把它纳入削减范围，否则资金不足时
  // 限价档减到 0 也压不下来，市价单会带着超限数量提交，而网关是整单校验、
  // 一票否决，结果整份订单计划被拒（实测线上拒单率 69% 正源于此）。
  if (buy_lots > max_buy_lots) {
    for (int level = m_config.levels - 1; level >= 0 && buy_lots > max_buy_lots; --level) {
      const int removed = std::min(lots[buy_limit_start + level], buy_lots - max_buy_lots);
      lots[buy_limit_start + level] -= removed;
      buy_lots -= removed;
    }
    if (buy_lots > max_buy_lots) {
      const int removed = std::min(lots[1], buy_lots - max_buy_lots);
      lots[1] -= removed;
      buy_lots -= removed;
    }
  }
  if (sell_lots > max_sell_lots) {
    for (int level = m_config.levels - 1; level >= 0 && sell_lots > max_sell_lots; --level) {
      const int removed = std::min(lots[sell_limit_start + level], sell_lots - max_sell_lots);
      lots[sell_limit_start + level] -= removed;
      sell_lots -= removed;
    }
    if (sell_lots > max_sell_lots) {
      const int removed = std::min(lots[sell_market_index], sell_lots - max_sell_lots);
      lots[sell_market_index] -= removed;
      sell_lots -= removed;
    }
  }
  return lots;
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::reconfigureOrders(
    const std::vector<double>& observation) {
  if (!m_book || m_book->bids.empty() || m_book->asks.empty()) co_return;

  const auto context = runtime_context();
  if (!context) {
    LOG(WARNING) << "[多层级做市] 未注入策略运行时上下文，无法提交订单计划";
    co_return;
  }

  updateTransition(observation);
  const auto action = m_policy.sample(observation);
  const auto lots = allocateLots(action);
  if (lots.size() != static_cast<std::size_t>(3 + 2 * m_config.levels)) co_return;

  {
    core::domain::OrderPlan plan;
    plan.plan_id = generateOrderId();
    plan.idempotency_key.value = plan.plan_id;
    plan.strategy_id = fmt::format("multilevel:{}", m_config.symbol);
    plan.symbol = m_config.symbol;
    plan.timestamp_ms = m_current_timestamp;
    plan.replace_policy = core::domain::ReplacePolicy::CANCEL_MISSING;

    auto add_intent = [&plan](const std::string& intent_id,
                              core::domain::Side side,
                              core::domain::OrderType type,
                              int level,
                              double price,
                              double quantity) {
      if (quantity <= 0.0 || price <= 0.0) return;
      core::domain::OrderIntent intent;
      intent.intent_id = intent_id;
      intent.symbol = plan.symbol;
      intent.side = side;
      intent.type = type;
      intent.level = level;
      intent.price = toDecimal(price);
      intent.quantity = toDecimal(quantity);
      plan.intents.push_back(std::move(intent));
    };

    if (lots[1] > 0) {
      add_intent("market-buy", core::domain::Side::BUY,
                 core::domain::OrderType::MARKET, -1, currentMidPrice(),
                 lots[1] * m_config.order_size);
    }
    if (lots[2 + m_config.levels] > 0) {
      add_intent("market-sell", core::domain::Side::SELL,
                 core::domain::OrderType::MARKET, -1, currentMidPrice(),
                 lots[2 + m_config.levels] * m_config.order_size);
    }
    // 报价基准是保留价而不是盘口价：保留价 = 中间价 − 库存偏斜。
    // 持有多头时保留价下移，双边报价随之下移，卖单更容易成交、买单更保守，
    // 从而把库存拉回中性，避免单边囤积后被迫以 taker 价平仓。
    const double mid = currentMidPrice();
    const double inventory_ratio = std::clamp(
        effectiveInventory() / std::max(m_config.inventory_limit, 1e-12), -1.0, 1.0);
    const double reservation =
        mid * (1.0 - m_config.inventory_skew_bps / 10000.0 * inventory_ratio);

    // 半价差下限取三者最大：盘口半价差、配置下限、半个最小变动价位。
    // 盘口价差常常只有 3 bps，而双边手续费 16 bps，贴着盘口报价等于每笔必亏；
    // 用费率量级的下限把报价推到能覆盖成本的位置（成交概率下降，但单笔有利可图）。
    const double best_bid = toDouble(m_book->bids.front().price);
    const double best_ask = toDouble(m_book->asks.front().price);
    const double book_half_spread = std::max(0.0, (best_ask - best_bid) / 2.0);
    const double half_spread = std::max(
        std::max(book_half_spread, mid * m_config.min_half_spread_bps / 10000.0),
        tickSize() * 0.5);

    // 档位步长：至少一跳；盘口档位间距更大时沿用盘口间距，保持与真实档位结构一致。
    double level_step = tickSize();
    if (m_book->bids.size() > 1) {
      level_step = std::max(level_step, std::abs(toDouble(m_book->bids[0].price) -
                                                 toDouble(m_book->bids[1].price)));
    }
    if (m_book->asks.size() > 1) {
      level_step = std::max(level_step, std::abs(toDouble(m_book->asks[0].price) -
                                                 toDouble(m_book->asks[1].price)));
    }

    for (int level = 0; level < m_config.levels; ++level) {
      const double offset = half_spread + level_step * level;
      add_intent(fmt::format("bid-{}", level), core::domain::Side::BUY,
                 core::domain::OrderType::LIMIT, level, reservation - offset,
                 lots[2 + level] * m_config.order_size);
      add_intent(fmt::format("ask-{}", level), core::domain::Side::SELL,
                 core::domain::OrderType::LIMIT, level, reservation + offset,
                 lots[3 + m_config.levels + level] * m_config.order_size);
    }

    const auto result = co_await context->submitAsync(plan);
    if (!result.accepted) {
      LOG(WARNING) << fmt::format("[多层级做市] 订单计划未提交: {}", result.error.message);
      co_return;
    }
    m_previous_action = action;
    m_last_decision_timestamp = m_current_timestamp > 0
        ? m_current_timestamp : m_last_decision_timestamp + 1;
    // 每次决策都会走到这里，长期运行时用 VLOG 避免刷屏。
    VLOG(1) << fmt::format("[多层级做市] 计划完成: intents={}, active_orders={}, inventory={}",
                           plan.intents.size(), context->activeOrders().size(),
                           effectiveInventory());
    co_return;
  }
}

std::string MultiLevelMarketMakingStrategy::generateOrderId() {
  return fmt::format("MM-{}-{}", m_config.symbol, ++m_order_sequence);
}

}  // namespace strategy::multilevel
