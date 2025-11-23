#ifndef __STRATEGY_BASE_STRATEGY_H__
#define __STRATEGY_BASE_STRATEGY_H__

/**
 * @file strategy.h
 * @brief 交易策略基类定义
 * 
 * 提供了策略的基础框架，包括：
 * - 事件接收接口（行情、账户、持仓等）
 * - 事件发送接口（订阅、查询等）
 */

#include "utils/utils.h"
#include "engine.h"

namespace strategy {
namespace base {

/**
 * @brief 策略基类
 * 
 * 所有交易策略必须继承此类并实现相应的事件处理方法。
 * 基类提供了与引擎交互的通用方法，如订阅行情、查询账户等。
 */
class Strategy : public std::enable_shared_from_this<Strategy>, public engine::Component {
public:
  /**
   * @brief 构造函数
   * @param engine 引擎指针，用于与系统其他组件交互
   */
  Strategy(engine::EnginePtr engine);
  ~Strategy();

  /**
   * @brief 初始化策略，注册事件回调函数
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> init() override;

  /**
   * @brief 发送消息事件
   * @param msg 消息数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> on_message(engine::MessageDataPtr msg);
  
  /**
   * @brief 请求查询账户信息
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> on_request_account();
  
  /**
   * @brief 请求查询持仓信息
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> on_request_position();
  
  /**
   * @brief 订阅订单簿数据
   * @param symbol 交易对符号
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> on_subscribe_book(const std::string& symbol);
  
  /**
   * @brief 订阅Tick数据
   * @param symbol 交易对符号
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> on_subscribe_tick(const std::string& symbol);

  /**
   * @brief 发送订单
   * @param order 订单数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> on_send_order(engine::OrderDataPtr order);

  /**
   * @brief 接收账户数据回调（纯虚函数，子类必须实现）
   * @param account 账户数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> recv_account(engine::AccountDataPtr account) = 0;
  
  /**
   * @brief 接收持仓数据回调（纯虚函数，子类必须实现）
   * @param position 持仓数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> recv_position(engine::PositionDataPtr position) = 0;
  
  /**
   * @brief 接收订单簿数据回调（纯虚函数，子类必须实现）
   * @param order 订单簿数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> recv_book(engine::BookPtr order) = 0;
  
  /**
   * @brief 接收Tick数据回调（纯虚函数，子类必须实现）
   * @param ticker Tick数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> recv_tick(engine::TickDataPtr ticker) = 0;

  /**
   * @brief 接收订单数据回调（纯虚函数，子类必须实现）
   * @param order 订单数据
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> recv_order(engine::OrderDataPtr order) = 0;

private:
  engine::EnginePtr _engine;  ///< 引擎指针
};

}  // namespace base
}  // namespace strategy

#endif  // __STRATEGY_BASE_STRATEGY_H__
