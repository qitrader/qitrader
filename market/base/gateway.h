#ifndef QITRADER_MARKET_BASE_GATEWAY_H_
#define QITRADER_MARKET_BASE_GATEWAY_H_

/**
 * @file gateway.h
 * @brief 交易所网关基类定义
 * 
 * 定义了与交易所交互的通用接口，包括：
 * - 连接管理
 * - 行情订阅
 * - 订单管理
 * - 账户和持仓查询
 */

#include <memory>

#include "engine.h"
#include "object.h"

namespace market::base {

using namespace engine;

/**
 * @brief 通用交易网关基类
 * 
 * 所有交易所网关必须继承此类并实现相应的虚函数。
 * 提供了与引擎交互的通用方法，如发送事件到引擎。
 */
class Gateway : public engine::Component, public std::enable_shared_from_this<Gateway> {
public:
  /**
   * @brief 构造函数
   * @param engine 引擎指针
   * @param name 网关名称（如"okx"）
   */
  Gateway(engine::EnginePtr engine, const std::string& name);
  virtual ~Gateway();

  /**
   * @brief 初始化网关，注册事件回调
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> init() override;

  /**
   * @brief 获取交易网关名称
   * @return std::string 网关名称
   */
  std::string name() const { return m_name; }

  // ========== 以下方法用于将数据发送到引擎 ==========
  
  /// 发送Tick数据到引擎
  asio::awaitable<void> on_tick(TickDataPtr tick);

  /// 发送Tick数据并等待引擎处理完成
  asio::awaitable<void> on_tick_sync(TickDataPtr tick);

  /// 发送 K 线数据到引擎
  asio::awaitable<void> on_bar(BarDataPtr bar);

  /// 发送 K 线数据并等待引擎处理完成
  asio::awaitable<void> on_bar_sync(BarDataPtr bar);

  /// 发送订单数据到引擎
  asio::awaitable<void> on_order(OrderDataPtr order);

  /// 发送持仓数据到引擎
  asio::awaitable<void> on_position(PositionDataPtr position);

  /// 发送账户数据到引擎
  asio::awaitable<void> on_account(AccountDataPtr account);

  /// 发送订单簿数据到引擎
  asio::awaitable<void> on_book(BookPtr book);

  /// 发送成交数据到引擎
  asio::awaitable<void> on_trade(TradeDataPtr trade);

  // ========== 以下是子类必须实现的虚函数 ==========
  
  /// 连接到交易所
  virtual void connect() = 0;
  
  /// 关闭与交易所的连接
  virtual void close() = 0;

  /// 取消订阅
  virtual void unsubscribe(const std::string& symbol) = 0;

  /// 发送订单
  virtual asio::awaitable<void> send_orders(OrderDataPtr order) = 0;
  
  /// 取消订单
  virtual asio::awaitable<void> cancel_order(OrderDataPtr order) = 0;

  /**
   * @brief 查询账户信息
   * @param data 查询请求数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> query_account(engine::QueryAccountDataPtr data) = 0;

  /**
   * @brief 查询持仓信息
   * @param data 查询请求数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> query_position(engine::QueryPositionDataPtr data) = 0;

  /**
   * @brief 查询历史订单
   * @param data 查询请求数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> query_order(engine::QueryOrderDataPtr data) = 0;

  /**
   * @brief 订阅订单簿数据
   * @param data 订阅请求数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> subscribe_book(engine::SubscribeDataPtr data) = 0;

  /**
   * @brief 订阅Tick数据
   * @param data 订阅请求数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> subscribe_tick(engine::SubscribeDataPtr data) = 0;

  /**
   * @brief 市场网关初始化，在init()中被调用
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> market_init() = 0;

protected:
  /// 请求引擎停止，用于回测等自主结束的网关
  asio::awaitable<void> stop_engine();

private:
  const std::string m_name;        ///< 网关名称
  std::weak_ptr<Engine> m_engine;  ///< 引擎弱引用，避免循环引用
};

}  // namespace market::base

#endif  // __MARKET_BASE_GATEWAY_H__
