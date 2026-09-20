#ifndef QITRADER_CORE_DOMAIN_TYPES_H_
#define QITRADER_CORE_DOMAIN_TYPES_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "utils/utils.h"

namespace core::domain {

enum class Side { BUY, SELL };
enum class OrderType { LIMIT, MARKET };
enum class OrderState {
  SUBMITTING,
  PENDING,
  PARTIAL_FILLED,
  FILLED,
  CANCELLED,
  REJECTED,
};

enum class ReplacePolicy { KEEP_EXISTING, CANCEL_MISSING, REPLACE_ALL };
enum class OrderOperationType { SUBMIT, CANCEL, REPLACE };
enum class ExecutionEventType { ACCEPTED, PENDING, PARTIAL_FILL, FILL, CANCELLED, REJECTED };

enum class ErrorCode {
  NONE,
  INVALID_ARGUMENT,
  RISK_REJECTED,
  QUEUE_FULL,
  DUPLICATE,
  NOT_FOUND,
  VENUE_ERROR,
  LEDGER_INCONSISTENT,
  UNSUPPORTED,
  INTERNAL_ERROR,
};

struct Error {
  ErrorCode code{ErrorCode::NONE};
  std::string message;
  explicit operator bool() const { return code != ErrorCode::NONE; }
};

enum class DiagnosticSeverity { INFO, WARNING, ERROR };

struct RuntimeDiagnostic {
  DiagnosticSeverity severity{DiagnosticSeverity::INFO};
  ErrorCode code{ErrorCode::NONE};
  std::string plan_id;
  std::string order_id;
  std::string message;
};

struct CommandResult {
  bool accepted{false};
  Error error;
  std::string command_id;
};

struct BookLevel {
  dec_float price{0};
  dec_float quantity{0};
};

struct MarketSnapshot {
  std::string symbol;
  std::string exchange;
  int64_t timestamp_ms{0};
  dec_float last_price{0};
  dec_float last_quantity{0};
  std::optional<dec_float> bid_price;
  std::optional<dec_float> ask_price;
  std::vector<BookLevel> bids;
  std::vector<BookLevel> asks;
};
using MarketSnapshotPtr = std::shared_ptr<const MarketSnapshot>;

struct MarketSubscription {
  std::string symbol;
  bool tick{false};
  bool book{false};
  bool bar{false};
};

struct IdempotencyKey {
  std::string value;

  bool empty() const { return value.empty(); }
  explicit operator bool() const { return !empty(); }
};

struct OrderIntent {
  std::string intent_id;
  std::string symbol;
  Side side{Side::BUY};
  OrderType type{OrderType::LIMIT};
  dec_float price{0};
  dec_float quantity{0};
  int level{-1};
  int64_t expire_at_ms{0};
  bool reduce_only{false};
};

struct OrderPlan {
  std::string plan_id;
  IdempotencyKey idempotency_key;
  std::string strategy_id;
  std::string symbol;
  int64_t timestamp_ms{0};
  ReplacePolicy replace_policy{ReplacePolicy::CANCEL_MISSING};
  std::vector<OrderIntent> intents;
};

struct OrderOperation {
  OrderOperationType type{OrderOperationType::SUBMIT};
  std::string order_id;
  std::optional<OrderIntent> intent;
};

struct OrderPlanDiff {
  std::string plan_id;
  std::string strategy_id;
  std::string symbol;
  std::vector<OrderOperation> operations;
};

struct ExecutionReport {
  ExecutionEventType type{ExecutionEventType::ACCEPTED};
  std::string execution_id;
  std::string order_id;
  std::string intent_id;
  std::string symbol;
  Side side{Side::BUY};
  OrderType order_type{OrderType::LIMIT};
  dec_float price{0};
  dec_float quantity{0};
  dec_float filled_quantity{0};
  dec_float fee{0};
  int64_t timestamp_ms{0};
  std::string reason;
};

struct ActiveOrder {
  std::string order_id;
  std::string strategy_id;
  OrderIntent intent;
  OrderState state{OrderState::SUBMITTING};
  dec_float filled_quantity{0};
};

struct PositionSnapshot {
  std::string symbol;
  Side side{Side::BUY};
  dec_float quantity{0};
  dec_float frozen_quantity{0};
  dec_float average_price{0};
  dec_float unrealized_pnl{0};
};

struct PortfolioSnapshot {
  uint64_t version{0};
  int64_t timestamp_ms{0};
  dec_float cash{0};
  dec_float frozen_cash{0};
  dec_float realized_pnl{0};
  dec_float fees{0};
  std::vector<PositionSnapshot> positions;
};
using PortfolioSnapshotPtr = std::shared_ptr<const PortfolioSnapshot>;

struct StrategyStateSnapshot {
  uint64_t version{0};
  int64_t timestamp_ms{0};
  std::vector<double> features;
};
using StrategyStateSnapshotPtr = std::shared_ptr<const StrategyStateSnapshot>;

}  // namespace core::domain

#endif  // QITRADER_CORE_DOMAIN_TYPES_H_
