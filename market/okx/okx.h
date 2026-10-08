#ifndef QITRADER_MARKET_OKX_OKX_H_
#define QITRADER_MARKET_OKX_OKX_H_

/**
 * @file okx.h
 * @brief OKX交易所网关实现
 *
 * 实现了与OKX交易所的交互，包括：
 * - 通过HTTP API查询账户、持仓、订单
 * - 通过WebSocket接收实时行情数据
 */

#include <atomic>
#include <map>
#include <set>
#include <string>

#include "base/gateway.h"
#include "config/config.h"
#include "engine.h"
#include "okx_http.h"
#include "okx_ws.h"
#include "utils/concurrent_map.hpp"
#include <boost/asio/steady_timer.hpp>

namespace market::okx {

struct SingleMarket {
  std::string symbol;
  engine::BookPtr last_book;      ///< 最近一次接收的订单簿数据
  engine::TickDataPtr last_tick;  ///< 最近一次接收的Tick数据
};

/**
 * @brief OKX交易所网关
 *
 * 继承自通用网关基类，实现了OKX交易所的具体功能。
 * 使用HTTP进行查询操作，使用WebSocket接收实时数据推送。
 */
class Okx : public base::Gateway {
 public:
  Okx(engine::EnginePtr engine);
  ~Okx() {}

  /// 连接功能（当前未实现）
  void connect() override{};

  /// 关闭连接功能（当前未实现）
  void close() override{};

  /**
   * @brief 主运行循环，持续从 WebSocket 接收数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> run() override;

  /**
   * @brief 引擎停止时调用：置停止标志并打断 WebSocket，让各协程退出
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> shutdown() override;

  /**
   * @brief 监听公共WebSocket，处理公共数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> watch_public();

  /**
   * @brief 监听私有WebSocket，处理私有数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> watch_private();

  /**
   * @brief 初始化市场网关，连接WebSocket
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> market_init() override;

  /// 取消订阅（当前未实现）
  void unsubscribe(const std::string& symbol) override{};

  /// 发送订单（当前未实现）
  asio::awaitable<void> send_orders(engine::OrderDataPtr order) override;

  /**
   * @brief 取消订单：调用 OKX 批量撤单接口，并把结果回报给引擎
   * @param order 待撤订单，每个子单携带引擎分配的内部 order_id
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> cancel_order(engine::OrderDataPtr order) override;

  /**
   * @brief 查询账户信息，通过HTTP API获取
   * @param data 查询请求数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> query_account(engine::QueryAccountDataPtr data) override;

  /**
   * @brief 查询持仓信息，通过HTTP API获取
   * @param data 查询请求数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> query_position(engine::QueryPositionDataPtr data) override;

  /**
   * @brief 查询历史订单，通过HTTP API获取
   * @param data 查询请求数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> query_order(engine::QueryOrderDataPtr data) override;

  /**
   * @brief 订阅订单簿数据，通过WebSocket订阅
   * @param data 订阅请求数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> subscribe_book(engine::SubscribeDataPtr data) override;

  /**
   * @brief 订阅Tick数据，通过WebSocket订阅
   * @param data 订阅请求数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> subscribe_tick(engine::SubscribeDataPtr data) override;

 private:
  /**
   * @brief 处理WebSocket接收到的订单簿数据
   * @param msg WebSocket消息
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> deal_book(const std::string& symbol, const std::vector<WsBook>& msg);

  /**
   * @brief 处理WebSocket接收到的Tick数据
   * @param msg WebSocket消息
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> deal_tick(const std::string& symbol, const std::vector<WsTick>& msg);

  asio::awaitable<void> deal_account(const Account& msg);
  asio::awaitable<void> deal_position(const std::vector<PositionDetail>& msg);
  asio::awaitable<void> deal_order(const std::vector<QueryOrderDetail>& msg);
  /**
   * @brief 登录私有WebSocket
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> ws_private_login();

  asio::awaitable<void> ws_private_subscribe_account();

  asio::awaitable<void> ws_private_subscribe_position();

  asio::awaitable<void> ws_private_subscribe_order();

  asio::awaitable<void> ws_deal(std::shared_ptr<OkxWs> ws);

  SendOrderRequest to_send_order_request(engine::OrderDataItemPtr order);
  SendOrderRequest to_send_order_request_spot(engine::OrderDataItemPtr order);
  SendOrderRequest to_send_order_request_swap(engine::OrderDataItemPtr order);

  /**
   * @brief 周期性发送应用层心跳（纯文本 "ping"）
   *
   * OKX 要求客户端 30s 内至少发送一次 ping，超时未收到会被服务端主动断开，
   * 而断线在这里表现为推送静默停止，很难排查。
   */
  asio::awaitable<void> ping_loop();

  /// 退避结束后重建连接并重新订阅；market_init() 抛异常只记日志，交给下一次退避
  asio::awaitable<void> reconnect();

  /// 重新订阅此前订阅过的行情品种（连接重建后订阅关系会丢失）
  asio::awaitable<void> resubscribe();

  /// 构造拒单回报项：沿用原子单的合约/价格/数量，状态置 REJECTED，
  /// 让上层据此释放为该单冻结的资金与持仓。
  static engine::OrderDataItemPtr make_rejected_item(const engine::OrderDataItemPtr& item);

  /// 内部 order_id -> 交易所订单标识。
  /// 撤单请求只带引擎分配的内部 order_id，而 OKX 撤单需要 instId + ordId，
  /// 只能靠这张表翻译（假设：引擎分配的 order_id 全局唯一）。
  struct OrderRef {
    std::string inst_id;  ///< 交易对，撤单接口的 instId
    std::string ord_id;   ///< 交易所订单号，撤单接口的 ordId
  };

  /// 心跳周期（秒）：OKX 要求 30s 内至少一次，留 10s 余量
  static constexpr int kPingIntervalS = 20;

  ConcurrentMap<std::string, SingleMarket> markets_;

  OkxHttp http_;  ///< HTTP客户端，用于查询操作
  std::shared_ptr<OkxWs> ws_public_;      ///< WebSocket客户端，用于接收实时数据
  std::shared_ptr<OkxWs> ws_private_;     ///< WebSocket客户端，用于接收私有数据

  /// 内部 order_id -> 交易所订单（单线程访问，不涉及跨线程竞争）
  std::map<std::string, OrderRef> order_refs_;
  /// 已订阅品种，连接重建后用于补订阅
  std::set<std::string> subscribed_symbols_;

  std::atomic<bool> stopped_{false};       ///< 停止标志：引擎停止或链路退出后置位
  std::atomic<bool> reconnecting_{false};  ///< 重连互斥：避免公共/私有两路同时重建
};

}  // namespace market::okx

#endif  // __MARKET_OKX_OKX_H__
