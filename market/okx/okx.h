#ifndef _BITCONITRADER_MARKET_OKX_OKX_H
#define _BITCONITRADER_MARKET_OKX_OKX_H

/**
 * @file okx.h
 * @brief OKX交易所网关实现
 *
 * 实现了与OKX交易所的交互，包括：
 * - 通过HTTP API查询账户、持仓、订单
 * - 通过WebSocket接收实时行情数据
 */

#include <string>

#include "base/gateway.h"
#include "config/config.h"
#include "engine.h"
#include "okx_http.h"
#include "okx_ws.h"
#include "utils/concurrent_map.hpp"

namespace market::okx {

struct SingleMarket : public std::enable_shared_from_this<SingleMarket> {
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
   * @brief 初始化市场网关，连接WebSocket
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> market_init() override;

  /// 取消订阅（当前未实现）
  void unsubscribe(const std::string& symbol) override{};

  /// 发送订单（当前未实现）
  asio::awaitable<void> send_orders(engine::OrderDataPtr order) override;

  /// 取消订单（当前未实现）
  asio::awaitable<void> cancel_order(engine::OrderDataPtr order) override { co_return; };

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
  asio::awaitable<void> deal_book(const WsMessage& msg);

  /**
   * @brief 处理WebSocket接收到的Tick数据
   * @param msg WebSocket消息
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> deal_tick(const WsMessage& msg);
  ConcurrentMap<std::string, SingleMarket> markets_;

  OkxHttp http_;  ///< HTTP客户端，用于查询操作
  OkxWs ws_;      ///< WebSocket客户端，用于接收实时数据
};

}  // namespace market::okx

#endif  // _BITCONITRADER_MARKET_OKX_OKX_H
