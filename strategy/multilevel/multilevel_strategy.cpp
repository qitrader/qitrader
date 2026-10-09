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
  // 不能用 std::to_string：它固定只输出 6 位小数，而构造函数已把 order_size
  // 下限放宽到 1e-8，小于 5e-7 的数量/价格会被量化成 "0.000000"，随后被
  // add_intent() 里的 quantity <= 0 / price <= 0 静默丢弃，小面额品种永远不下单。
  // 17 位有效数字是 double 的无损往返精度，既保得住 1e-8 量级，也不会引入虚假尾数。
  return dec_float(fmt::format("{:.17g}", value));
}

/// 量化方向：买单向下、卖单向上，避免把买卖报价压到同一价位。
enum class TickRounding { NEAREST, DOWN, UP };

/// 报价量化步长：按价格量级取固定的十进制精度。
/// 不能用随盘口/中价变化的 tickSize() 来量化：tick 本身每帧都在变，
/// 量化后的价格照样每帧不同，起不到稳定报价的作用（撤单率不会下降）。
double quoteTick(double price) {
  if (!(price > 0.0)) return 0.0;
  if (price >= 1000.0) return 0.1;
  if (price >= 100.0) return 0.01;
  if (price >= 10.0) return 0.001;
  if (price >= 1.0) return 0.0001;
  return 0.000001;
}

/// 把价格量化到 tick 的整数倍后再写进 intent。
/// 报价价 = 保留价 ± 偏移，是随中价连续变化的量；不量化时中价每动一点，
/// 价格字符串就变，OrderManager 会把所有档位判成"需换单"（线上实测每帧
/// 全撤全挂、撤单率 83%）。量化后只有跨过半个 tick 才产生新价格。
/// 量化在十进制空间做（整数步数 × tick 的十进制值），避免浮点乘除引入尾数。
dec_float toDecimal(double value, double tick, TickRounding rounding) {
  if (!(tick > 0.0) || !std::isfinite(value)) return toDecimal(value);
  double steps = 0.0;
  switch (rounding) {
    case TickRounding::DOWN: steps = std::floor(value / tick); break;
    case TickRounding::UP: steps = std::ceil(value / tick); break;
    case TickRounding::NEAREST: steps = std::round(value / tick); break;
  }
  if (!std::isfinite(steps)) return toDecimal(value);
  return dec_float(fmt::format("{:.0f}", steps)) * dec_float(fmt::format("{:.17g}", tick));
}

}  // namespace

MultiLevelMarketMakingStrategy::MultiLevelMarketMakingStrategy(MultiLevelConfig config)
    : m_config(std::move(config)),
      m_policy(6 + 2 * static_cast<std::size_t>(std::max(1, m_config.levels)),
               3 + 2 * static_cast<std::size_t>(std::max(1, m_config.levels)),
               m_config.learning_rate, m_config.exploration) {
  m_config.levels = std::max(1, m_config.levels);
  m_config.order_budget = std::max(1, m_config.order_budget);
  m_config.order_size = std::max(0.00000001, m_config.order_size);
  m_config.inventory_limit = std::max(m_config.order_size, m_config.inventory_limit);
  m_config.decision_interval_ms = std::max<int64_t>(0, m_config.decision_interval_ms);
  // 配置指纹写入权重文件：levels / min_half_spread_bps 变了以后，旧权重的
  // 观测与动作语义都不再成立（例如半价差 6bps -> 24bps），加载时必须被拒绝，
  // 而不是静默复用一份已经失效的策略。
  m_policy.setConfigFingerprint(fmt::format("levels={},min_half_spread_bps={:g}",
                                            m_config.levels, m_config.min_half_spread_bps));
  loadModel();
}

bool MultiLevelMarketMakingStrategy::loadModel() {
  if (m_config.reset_model) {
    m_policy.reset();
    LOG(INFO) << "[多层级做市] reset_model=true：已回到初始权重，不加载已有模型";
    return false;
  }
  // 读取路径可单独指定：默认与保存路径相同，评估/换档时读旧、写新互不干扰。
  const std::string path = m_config.model_load_path.empty() ? m_config.model_path
                                                            : m_config.model_load_path;
  if (path.empty()) return false;

  std::ifstream probe(path);
  if (!probe.good()) {
    LOG(INFO) << fmt::format("[多层级做市] 未找到模型 {}，从初始权重开始学习", path);
    return false;
  }

  const bool ok = m_policy.load(path);
  if (ok) {
    LOG(INFO) << fmt::format("[多层级做市] 已加载模型: {}", path);
  } else {
    // 维度或配置指纹不匹配：这份权重对当前配置已无意义，退回初始权重。
    LOG(WARNING) << fmt::format("[多层级做市] 模型 {} 与当前配置不匹配，改用初始权重", path);
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
  // 只推理模式不落盘：评估过程中权重没有变化，保存只是把同一份内容写回去，
  // 却会盖掉写入时间等元信息，也容易让"评估结果"与"训练结果"混淆。
  if (!m_config.eval_only) saveModel();
  co_return;
}

asio::awaitable<void> MultiLevelMarketMakingStrategy::run() {
  LOG(INFO) << fmt::format(
      "[多层级做市] 启动: symbol={}, levels={}, budget={}, size={}, inventory_limit={}, interval={}ms",
      m_config.symbol, m_config.levels, m_config.order_budget, m_config.order_size,
      m_config.inventory_limit, m_config.decision_interval_ms);
  co_return;
}

void MultiLevelMarketMakingStrategy::onMarket(const core::domain::MarketSnapshot& snapshot) {
  if (snapshot.symbol != m_config.symbol || snapshot.last_price <= 0) return;

  m_last_price = toDouble(snapshot.last_price);
  if (!snapshot.bids.empty() && !snapshot.asks.empty()) {
    m_market = snapshot;
  } else {
    m_market = makeSyntheticBook(snapshot.last_price, snapshot.timestamp_ms);
  }
  if (m_market && !m_market->bids.empty() && !m_market->asks.empty()) {
    const double bid = toDouble(m_market->bids.front().price);
    const double ask = toDouble(m_market->asks.front().price);
    if (ask >= bid && bid > 0.0) m_tick_size = std::max(ask - bid, bid * 0.0001);
  }
  if (m_previous_cash == 0.0) m_previous_cash = effectiveCash();

  // 必须用行情时间戳推进策略时钟，否则决策间隔判断永远成立。
  m_current_timestamp = snapshot.timestamp_ms;
  const bool first_decision = m_last_decision_timestamp == 0;
  const bool interval_elapsed = first_decision ||
      m_config.decision_interval_ms == 0 ||
      m_current_timestamp - m_last_decision_timestamp >= m_config.decision_interval_ms;
  if (interval_elapsed) reconfigureOrders(makeObservation());
}

double MultiLevelMarketMakingStrategy::currentMidPrice() const {
  if (m_market && !m_market->bids.empty() && !m_market->asks.empty()) {
    const double bid = toDouble(m_market->bids.front().price);
    const double ask = toDouble(m_market->asks.front().price);
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

core::domain::MarketSnapshot MultiLevelMarketMakingStrategy::makeSyntheticBook(
    const dec_float& price, int64_t timestamp_ms) const {
  const double mid = toDouble(price);
  const double step = std::max(0.01, mid * 0.0001);
  core::domain::MarketSnapshot book;
  book.symbol = m_config.symbol;
  book.exchange = "synthetic";
  book.timestamp_ms = timestamp_ms;
  book.last_price = price;
  for (int level = 0; level < m_config.levels; ++level) {
    book.bids.push_back({toDecimal(mid - step * (level + 1)), dec_float("10")});
    book.asks.push_back({toDecimal(mid + step * (level + 1)), dec_float("10")});
  }
  if (!book.bids.empty()) book.bid_price = book.bids.front().price;
  if (!book.asks.empty()) book.ask_price = book.asks.front().price;
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

  if (m_market && !m_market->bids.empty() && !m_market->asks.empty()) {
    const double bid = toDouble(m_market->bids.front().price);
    const double ask = toDouble(m_market->asks.front().price);
    spread = std::max(0.0, ask - bid);
    const std::size_t bid_count = std::min<std::size_t>(m_market->bids.size(), m_config.levels);
    const std::size_t ask_count = std::min<std::size_t>(m_market->asks.size(), m_config.levels);
    for (std::size_t i = 0; i < bid_count; ++i) bid_depth += toDouble(m_market->bids[i].quantity);
    for (std::size_t i = 0; i < ask_count; ++i) ask_depth += toDouble(m_market->asks[i].quantity);
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
    // eval_only：只推理不学习。评估已有权重时若继续 update，
    // 评估过程本身会把权重改掉，且周期性落盘会覆盖掉被评估的那份权重。
    if (!m_config.eval_only) {
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
    }  // !eval_only
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

void MultiLevelMarketMakingStrategy::reconfigureOrders(
    const std::vector<double>& observation) {
  if (!m_market || m_market->bids.empty() || m_market->asks.empty()) return;

  const auto context = runtime_context();
  if (!context) {
    LOG(WARNING) << "[多层级做市] 未注入策略运行时上下文，无法提交订单计划";
    return;
  }

  updateTransition(observation);
  const auto action = m_policy.sample(observation);
  const auto lots = allocateLots(action);
  if (lots.size() != static_cast<std::size_t>(3 + 2 * m_config.levels)) return;

  {
    core::domain::OrderPlan plan;
    plan.plan_id = generateOrderId();
    plan.idempotency_key.value = plan.plan_id;
    plan.strategy_id = fmt::format("multilevel:{}", m_config.symbol);
    plan.symbol = m_config.symbol;
    plan.timestamp_ms = m_current_timestamp;
    plan.replace_policy = core::domain::ReplacePolicy::CANCEL_MISSING;

    // 报价按固定精度量化：中价小幅抖动不再产生新价格，未变化的档位可以被复用，
    // 而不是每帧全撤全挂（线上撤单率 83% 的主因）。步长必须与中价无关，
    // 否则量化结果依然每帧变化。
    const double tick = quoteTick(currentMidPrice());
    std::unordered_set<std::string> quoted_ids;
    auto add_intent = [this, &plan, &quoted_ids, tick](const std::string& intent_id,
                                                       core::domain::Side side,
                                                       core::domain::OrderType type,
                                                       int level,
                                                       double price,
                                                       double quantity) {
      if (quantity <= 0.0 || price <= 0.0) return;
      // 买单向下、卖单向上取整，避免量化把两侧报价压到同一价位。
      const TickRounding rounding =
          type == core::domain::OrderType::MARKET
              ? TickRounding::NEAREST
              : (side == core::domain::Side::BUY ? TickRounding::DOWN : TickRounding::UP);
      dec_float quantized_price = toDecimal(price, tick, rounding);
      if (quantized_price <= 0) return;
      // 重新报价阈值：目标价相对在途报价的偏移小于阈值时沿用旧报价。
      // 中价每帧都在动，逐帧重挂会把撤单率推到 80% 以上，实盘还要额外
      // 付出 API 限额；报价滞后一点点远好过每次决策都全撤全挂。
      const auto previous = m_last_quotes.find(intent_id);
      if (type != core::domain::OrderType::MARKET &&
          m_config.requote_threshold_bps > 0.0 && previous != m_last_quotes.end() &&
          previous->second > 0) {
        const dec_float diff = quantized_price > previous->second
            ? quantized_price - previous->second : previous->second - quantized_price;
        const dec_float threshold =
            previous->second * dec_float(m_config.requote_threshold_bps / 10000.0);
        if (diff <= threshold) quantized_price = previous->second;
      }
      m_last_quotes[intent_id] = quantized_price;
      quoted_ids.insert(intent_id);
      core::domain::OrderIntent intent;
      intent.intent_id = intent_id;
      intent.symbol = plan.symbol;
      intent.side = side;
      intent.type = type;
      intent.level = level;
      intent.price = quantized_price;
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
    const double best_bid = toDouble(m_market->bids.front().price);
    const double best_ask = toDouble(m_market->asks.front().price);
    const double book_half_spread = std::max(0.0, (best_ask - best_bid) / 2.0);
    const double half_spread = std::max(
        std::max(book_half_spread, mid * m_config.min_half_spread_bps / 10000.0),
        tickSize() * 0.5);

    // 档位步长：至少一跳；盘口档位间距更大时沿用盘口间距，保持与真实档位结构一致。
    double level_step = tickSize();
    if (m_market->bids.size() > 1) {
      level_step = std::max(level_step, std::abs(toDouble(m_market->bids[0].price) -
                                                 toDouble(m_market->bids[1].price)));
    }
    if (m_market->asks.size() > 1) {
      level_step = std::max(level_step, std::abs(toDouble(m_market->asks[0].price) -
                                                 toDouble(m_market->asks[1].price)));
    }
    // 档位间距不得小于量化步长，否则相邻档位会被量化到同一个价格上。
    level_step = std::max(level_step, quoteTick(mid));

    for (int level = 0; level < m_config.levels; ++level) {
      const double offset = half_spread + level_step * level;
      add_intent(fmt::format("bid-{}", level), core::domain::Side::BUY,
                 core::domain::OrderType::LIMIT, level, reservation - offset,
                 lots[2 + level] * m_config.order_size);
      add_intent(fmt::format("ask-{}", level), core::domain::Side::SELL,
                 core::domain::OrderType::LIMIT, level, reservation + offset,
                 lots[3 + m_config.levels + level] * m_config.order_size);
    }

    // 本帧没有报价的档位（预算为 0）会被撤掉，其历史报价不再代表"在途报价"，
    // 必须清掉，否则该档位下次出现时会沿用一份已经过期的价格。
    for (auto it = m_last_quotes.begin(); it != m_last_quotes.end();) {
      if (quoted_ids.contains(it->first)) {
        ++it;
      } else {
        it = m_last_quotes.erase(it);
      }
    }

    const auto result = context->submit(plan);
    if (!result.accepted) {
      LOG(WARNING) << fmt::format("[多层级做市] 订单计划未提交: {}", result.error.message);
      return;
    }
    m_previous_action = action;
    m_last_decision_timestamp = m_current_timestamp > 0
        ? m_current_timestamp : m_last_decision_timestamp + 1;
    // 每次决策都会走到这里，长期运行时用 VLOG 避免刷屏。
    VLOG(1) << fmt::format("[多层级做市] 计划完成: intents={}, active_orders={}, inventory={}",
                           plan.intents.size(), context->activeOrders().size(),
                           effectiveInventory());
  }
}

std::string MultiLevelMarketMakingStrategy::generateOrderId() {
  return fmt::format("MM-{}-{}", m_config.symbol, ++m_order_sequence);
}

}  // namespace strategy::multilevel
