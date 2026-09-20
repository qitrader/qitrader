#include "portfolio_ledger.h"

namespace core::portfolio {

PortfolioLedger::PortfolioLedger(const dec_float& initial_cash) {
  m_snapshot.cash = initial_cash;
}

domain::PositionSnapshot* PortfolioLedger::findPosition(const std::string& symbol) {
  for (auto& position : m_snapshot.positions) {
    if (position.symbol == symbol) return &position;
  }
  return nullptr;
}

const domain::PositionSnapshot* PortfolioLedger::findPosition(
    const std::string& symbol) const {
  for (const auto& position : m_snapshot.positions) {
    if (position.symbol == symbol) return &position;
  }
  return nullptr;
}

void PortfolioLedger::bumpVersion(int64_t timestamp_ms) {
  ++m_snapshot.version;
  if (timestamp_ms > 0) m_snapshot.timestamp_ms = timestamp_ms;
}

domain::CommandResult PortfolioLedger::reserve(const domain::OrderIntent& intent,
                                                const dec_float& quantity) {
  if (quantity <= 0 || intent.symbol.empty()) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid reservation"}, {}};
  }
  if (intent.side == domain::Side::BUY) {
    const dec_float notional = intent.type == domain::OrderType::MARKET
        ? intent.price * quantity : intent.price * quantity;
    if (m_snapshot.cash - m_snapshot.frozen_cash < notional) {
      return {false, {domain::ErrorCode::RISK_REJECTED, "insufficient available cash"}, {}};
    }
    m_snapshot.frozen_cash += notional;
  } else {
    auto* position = findPosition(intent.symbol);
    if (!position || position->quantity - position->frozen_quantity < quantity) {
      return {false, {domain::ErrorCode::RISK_REJECTED, "insufficient available position"}, {}};
    }
    position->frozen_quantity += quantity;
  }
  bumpVersion(0);
  return {true, {}, {}};
}

domain::CommandResult PortfolioLedger::release(const domain::OrderIntent& intent,
                                                const dec_float& quantity) {
  if (quantity <= 0 || intent.symbol.empty()) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid release"}, {}};
  }
  if (intent.side == domain::Side::BUY) {
    const dec_float amount = intent.price * quantity;
    const dec_float remaining = m_snapshot.frozen_cash - amount;
    m_snapshot.frozen_cash = remaining > 0 ? remaining : dec_float(0);
  } else if (auto* position = findPosition(intent.symbol)) {
    const dec_float remaining = position->frozen_quantity - quantity;
    position->frozen_quantity = remaining > 0 ? remaining : dec_float(0);
  }
  bumpVersion(0);
  return {true, {}, {}};
}

domain::CommandResult PortfolioLedger::apply(const domain::ExecutionReport& report) {
  if (report.execution_id.empty()) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "execution id is empty"}, {}};
  }
  if (!m_processed_executions.insert(report.execution_id).second) {
    return {true, {}, report.execution_id};
  }
  if (report.symbol.empty() || report.quantity < 0 || report.filled_quantity < 0) {
    return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid execution report"}, {}};
  }

  if (report.type == domain::ExecutionEventType::FILL ||
      report.type == domain::ExecutionEventType::PARTIAL_FILL) {
    const dec_float quantity = report.filled_quantity > 0
        ? report.filled_quantity : report.quantity;
    if (quantity <= 0 || report.price <= 0) {
      return {false, {domain::ErrorCode::INVALID_ARGUMENT, "invalid fill quantity or price"}, {}};
    }

    auto* position = findPosition(report.symbol);
    if (!position) {
      m_snapshot.positions.push_back({});
      position = &m_snapshot.positions.back();
      position->symbol = report.symbol;
      position->side = domain::Side::BUY;
    }

    if (report.side == domain::Side::BUY) {
      const dec_float cost = report.price * quantity;
      const dec_float total_cost = position->average_price * position->quantity + cost;
      position->quantity += quantity;
      position->average_price = position->quantity > 0
          ? total_cost / position->quantity : dec_float(0);
      m_snapshot.cash -= cost;
    } else {
      const dec_float revenue = report.price * quantity;
      const dec_float pnl = (report.price - position->average_price) * quantity;
      const dec_float remaining = position->quantity - quantity;
      position->quantity = remaining > 0 ? remaining : dec_float(0);
      if (position->quantity == 0) position->average_price = dec_float(0);
      m_snapshot.cash += revenue;
      m_snapshot.realized_pnl += pnl;
    }
    m_snapshot.fees += report.fee;
    m_snapshot.cash -= report.fee;
  }

  bumpVersion(report.timestamp_ms);
  return {true, {}, report.execution_id};
}

void PortfolioLedger::markToMarket(const std::string& symbol, const dec_float& price,
                                   int64_t timestamp_ms) {
  auto* position = findPosition(symbol);
  if (!position) return;
  position->unrealized_pnl = (price - position->average_price) * position->quantity;
  bumpVersion(timestamp_ms);
}

domain::PortfolioSnapshotPtr PortfolioLedger::snapshot() const {
  return std::make_shared<const domain::PortfolioSnapshot>(m_snapshot);
}

}  // namespace core::portfolio
