#include "legacy_ledger_adapter.h"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace core::portfolio {

namespace {

/// 现金校验的相对容差，低于该比例的差异视为浮点噪声。
constexpr double kCashRelativeTolerance = 0.001;
/// 现金校验的绝对容差。
constexpr double kCashAbsoluteTolerance = 0.01;
/// 持仓数量校验的绝对容差。
constexpr double kPositionTolerance = 1e-8;
/// 校准日志的最小输出间隔（毫秒）：成交密集时限频，避免刷屏。
constexpr int64_t kNotifyIntervalMs = 60000;

double toDouble(const dec_float& value) {
  return value.convert_to<double>();
}

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

domain::PositionSnapshot toPositionSnapshot(const engine::PositionItem& item) {
  domain::PositionSnapshot snapshot;
  snapshot.symbol = item.symbol;
  snapshot.side = item.direction == engine::Direction::BUY ? domain::Side::BUY
                                                           : domain::Side::SELL;
  snapshot.quantity = item.volume;
  snapshot.frozen_quantity = item.frozen_volume;
  snapshot.average_price = item.price;
  snapshot.unrealized_pnl = item.pnl;
  return snapshot;
}

}  // namespace

LegacyLedgerAdapter::LegacyLedgerAdapter(engine::EnginePtr engine,
                                         std::shared_ptr<PortfolioLedger> ledger,
                                         InconsistencyLogger logger)
    : m_engine(std::move(engine)),
      m_ledger(std::move(ledger)),
      m_logger(std::move(logger)) {
  if (!m_engine) return;

  // 注册到引擎事件，由适配器而不是策略负责把 Legacy 状态写入新账本。
  m_engine->register_callback<engine::AccountData>(
      engine::EventType::kAccount,
      [this](engine::AccountDataPtr account) -> asio::awaitable<void> {
        if (account) onAccount(*account);
        co_return;
      });
  m_engine->register_callback<engine::PositionData>(
      engine::EventType::kPosition,
      [this](engine::PositionDataPtr position) -> asio::awaitable<void> {
        if (position) onPosition(*position);
        co_return;
      });
}

bool LegacyLedgerAdapter::canAdoptLegacyState() const {
  // 只有账本自身还没有任何执行回报时才允许采纳 Legacy 初始状态，
  // 否则运行时已经开始记账，覆盖会造成状态回退。
  return m_ledger && !m_ledger->hasExecutionUpdates();
}

void LegacyLedgerAdapter::onAccount(const engine::AccountData& account) {
  if (!m_ledger) return;
  m_latest_cash = account.balance;

  if (!m_cash_synced && canAdoptLegacyState()) {
    m_ledger->syncCash(account.balance, account.frozen_balance, account.timestamp_ms);
    m_cash_synced = true;
    return;
  }
  reconcileCash(account.balance, account.frozen_balance, account.timestamp_ms);
}

void LegacyLedgerAdapter::onPosition(const engine::PositionData& position) {
  if (!m_ledger) return;

  std::vector<domain::PositionSnapshot> snapshots;
  for (const auto& item : position.items) {
    if (!item || item->symbol.empty()) continue;
    snapshots.push_back(toPositionSnapshot(*item));
  }
  m_latest_positions = snapshots;

  if (!m_position_synced && canAdoptLegacyState()) {
    m_ledger->syncPositions(snapshots, position.timestamp_ms);
    m_position_synced = true;
    return;
  }
  reconcilePositions(snapshots, position.timestamp_ms);
}

bool LegacyLedgerAdapter::reconcileCash(const dec_float& cash, const dec_float& frozen,
                                        int64_t timestamp_ms) {
  const auto snapshot = m_ledger->snapshot();
  if (!snapshot) return false;

  const double expected = toDouble(cash);
  const double actual = toDouble(snapshot->cash);
  const double diff = std::fabs(expected - actual);
  if (diff <= kCashAbsoluteTolerance) return false;
  const double scale = std::max(1.0, std::fabs(actual));
  if (diff / scale <= kCashRelativeTolerance) return false;

  // 执行回报链路不完整（成交回报不带手续费），统一账本会系统性高于网关，
  // 策略据此挂出的买单会超出真实可用资金而被网关拒绝，因此以网关账本为准校正。
  m_ledger->syncCash(cash, frozen, timestamp_ms);
  ++m_reconcile_count;
  notify(fmt::format("现金已按 Legacy 账本校正: Legacy={:.4f} 统一账本={:.4f} 差异={:.4f}",
                     expected, actual, diff));
  return true;
}

bool LegacyLedgerAdapter::reconcilePositions(
    const std::vector<domain::PositionSnapshot>& snapshots, int64_t timestamp_ms) {
  const auto current = m_ledger->snapshot();
  if (!current) return false;

  bool mismatch = false;
  std::string detail;
  for (const auto& expected : snapshots) {
    const domain::PositionSnapshot* actual = nullptr;
    for (const auto& position : current->positions) {
      if (position.symbol == expected.symbol) {
        actual = &position;
        break;
      }
    }
    if (!actual) {
      if (expected.quantity > 0) {
        mismatch = true;
        detail = fmt::format("{} 统一账本无持仓，Legacy={}",
                             expected.symbol, expected.quantity.str());
      }
      continue;
    }
    const double diff = std::fabs(toDouble(expected.quantity) - toDouble(actual->quantity));
    if (diff <= kPositionTolerance) continue;
    mismatch = true;
    detail = fmt::format("{} Legacy={} 统一账本={}",
                         expected.symbol, expected.quantity.str(), actual->quantity.str());
  }
  // 网关已平掉、统一账本仍残留的持仓同样要清掉，否则会被当成可卖库存继续挂卖单。
  for (const auto& position : current->positions) {
    if (position.quantity <= 0) continue;
    const bool exists = std::any_of(
        snapshots.begin(), snapshots.end(),
        [&](const domain::PositionSnapshot& s) { return s.symbol == position.symbol; });
    if (exists) continue;
    mismatch = true;
    detail = fmt::format("{} 网关已平仓但统一账本仍为 {}",
                         position.symbol, position.quantity.str());
    break;
  }

  if (!mismatch) return false;
  m_ledger->syncPositions(snapshots, timestamp_ms);
  ++m_reconcile_count;
  notify(fmt::format("持仓已按 Legacy 账本校正: {}", detail));
  return true;
}

void LegacyLedgerAdapter::notify(const std::string& message) {
  if (!m_logger) return;
  const int64_t now = nowMs();
  if (m_last_notify_ms > 0 && now - m_last_notify_ms < kNotifyIntervalMs) {
    ++m_suppressed_logs;
    return;
  }
  m_last_notify_ms = now;
  if (m_suppressed_logs == 0) {
    m_logger(message);
    return;
  }
  m_logger(fmt::format("{}（期间另有 {} 次校正未输出）", message, m_suppressed_logs));
  m_suppressed_logs = 0;
}

std::vector<std::string> LegacyLedgerAdapter::verifyFinal() const {
  std::vector<std::string> mismatches;
  if (!m_ledger || !m_cash_synced) return mismatches;

  collectCashMismatch(m_latest_cash, mismatches);
  collectPositionMismatches(m_latest_positions, mismatches);

  if (m_logger) {
    for (const auto& message : mismatches) m_logger(message);
  }
  return mismatches;
}

void LegacyLedgerAdapter::collectCashMismatch(const dec_float& legacy_cash,
                                              std::vector<std::string>& mismatches) const {
  const auto snapshot = m_ledger->snapshot();
  if (!snapshot) return;

  const double expected = toDouble(legacy_cash);
  const double actual = toDouble(snapshot->cash);
  const double diff = std::fabs(expected - actual);
  if (diff <= kCashAbsoluteTolerance) return;

  const double scale = std::max(1.0, std::fabs(actual));
  if (diff / scale <= kCashRelativeTolerance) return;

  mismatches.push_back(fmt::format("现金状态不一致: Legacy={:.4f} 统一账本={:.4f} 差异={:.4f}",
                                   expected, actual, diff));
}

void LegacyLedgerAdapter::collectPositionMismatches(
    const std::vector<domain::PositionSnapshot>& legacy,
    std::vector<std::string>& mismatches) const {
  const auto snapshot = m_ledger->snapshot();
  if (!snapshot) return;

  for (const auto& expected : legacy) {
    const domain::PositionSnapshot* actual = nullptr;
    for (const auto& position : snapshot->positions) {
      if (position.symbol == expected.symbol) {
        actual = &position;
        break;
      }
    }
    if (!actual) {
      if (expected.quantity > 0) {
        mismatches.push_back(fmt::format("持仓缺失: {} Legacy={} 但统一账本无该品种持仓",
                                         expected.symbol, expected.quantity.str()));
      }
      continue;
    }
    const double diff = std::fabs(toDouble(expected.quantity) - toDouble(actual->quantity));
    if (diff <= kPositionTolerance) continue;
    mismatches.push_back(fmt::format("持仓数量不一致: {} Legacy={} 统一账本={} 差异={}",
                                     expected.symbol, expected.quantity.str(),
                                     actual->quantity.str(), diff));
  }
}

}  // namespace core::portfolio
