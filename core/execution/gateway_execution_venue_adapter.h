#ifndef QITRADER_CORE_EXECUTION_GATEWAY_EXECUTION_VENUE_ADAPTER_H_
#define QITRADER_CORE_EXECUTION_GATEWAY_EXECUTION_VENUE_ADAPTER_H_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "core/execution/execution_venue.h"
#include "engine/engine.h"

namespace core::execution {

/**
 * @brief 将通用执行端口适配到现有 Engine/Gateway 事件协议。
 *
 * submit/cancel 只负责把命令异步投递到 Engine，订单结果通过 ExecutionCallback 返回。
 * 因此策略行情回调不会等待网关或撮合器完成处理。
 */
class GatewayExecutionVenueAdapter final : public ExecutionVenue,
                                            public std::enable_shared_from_this<GatewayExecutionVenueAdapter> {
 public:
  explicit GatewayExecutionVenueAdapter(engine::EnginePtr engine);

  domain::CommandResult submit(const std::string& order_id,
                               const domain::OrderIntent& intent) override;
  domain::CommandResult cancel(const std::string& order_id) override;
  VenueCapabilities capabilities() const override;
  void setCallback(ExecutionCallback callback) override;

  /// 关闭执行端口，停止阶段的命令会被直接丢弃。
  void close() { m_closed.store(true); }
  bool closed() const { return m_closed.load(); }

  /// 等待已投递的引擎事件命令执行完成。
  bool waitIdle(uint32_t timeout_ms);

 private:
  asio::awaitable<void> dispatchOrderGuarded(const std::string& order_id,
                                             const domain::OrderIntent& intent);
  asio::awaitable<void> dispatchCancelGuarded(const std::string& order_id);
  asio::awaitable<void> dispatchOrder(const std::string& order_id,
                                      const domain::OrderIntent& intent);
  asio::awaitable<void> dispatchCancel(const std::string& order_id);
  void beginDispatch();
  void endDispatch();
  asio::awaitable<void> onOrder(engine::OrderDataPtr order);
  static domain::ExecutionEventType mapState(engine::OrderStatus status);

  engine::EnginePtr m_engine;
  ExecutionCallback m_callback;
  std::unordered_map<std::string, domain::OrderIntent> m_intents;
  std::atomic<bool> m_closed{false};
  std::mutex m_dispatch_mutex;
  std::condition_variable m_dispatch_cv;
  std::size_t m_in_flight{0};
};

}  // namespace core::execution

#endif  // QITRADER_CORE_EXECUTION_GATEWAY_EXECUTION_VENUE_ADAPTER_H_
