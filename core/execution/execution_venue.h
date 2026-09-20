#ifndef QITRADER_CORE_EXECUTION_EXECUTION_VENUE_H_
#define QITRADER_CORE_EXECUTION_EXECUTION_VENUE_H_

#include <functional>
#include <string>

#include "core/domain/types.h"

namespace core::execution {

struct VenueCapabilities {
  bool supports_cancel{true};
  bool supports_replace{false};
  bool supports_batch_orders{false};
  bool supports_partial_fill{false};
};

using ExecutionCallback = std::function<void(const domain::ExecutionReport&)>;

/**
 * @brief 与交易所、Paper 或回测撮合器交互的统一执行端口。
 */
class ExecutionVenue {
 public:
  virtual ~ExecutionVenue() = default;
  virtual domain::CommandResult submit(const std::string& order_id,
                                       const domain::OrderIntent& intent) = 0;
  virtual domain::CommandResult cancel(const std::string& order_id) = 0;
  virtual VenueCapabilities capabilities() const = 0;
  virtual void setCallback(ExecutionCallback callback) = 0;
};

}  // namespace core::execution

#endif  // QITRADER_CORE_EXECUTION_EXECUTION_VENUE_H_
