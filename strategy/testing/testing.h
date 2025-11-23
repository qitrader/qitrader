#ifndef __STRAGE_TESTING_TESTING_H__
#define __STRAGE_TESTING_TESTING_H__

/**
 * @file testing.h
 * @brief 测试策略实现
 * 
 * 这是一个简单的测试策略，用于验证系统功能。
 * 主要功能：
 * - 订阅BTC-USDT的行情数据
 * - 查询账户和持仓信息
 * - 打印接收到的数据
 */

#include <string>
#include "base/strategy.h"

namespace strategy {
namespace testing {

/**
 * @brief 测试策略类
 * 
 * 继承自策略基类，实现了基本的数据接收和打印功能。
 */
class Testing : public base::Strategy {
public:
  Testing(engine::EnginePtr engine);
  ~Testing();

  /**
   * @brief 策略主运行逻辑
   * 
   * 启动时执行以下操作：
   * 1. 查询账户信息
   * 2. 查询持仓信息
   * 3. 订阅BTC-USDT的订单簿和Tick数据
   * 
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> run() override;

  /// 接收并打印账户数据
  asio::awaitable<void> recv_account(engine::AccountDataPtr account) override;
  
  /// 接收并打印持仓数据
  asio::awaitable<void> recv_position(engine::PositionDataPtr position) override;
  
  /// 接收并打印订单簿数据
  asio::awaitable<void> recv_book(engine::BookPtr order) override;
  
  /// 接收并打印Tick数据
  asio::awaitable<void> recv_tick(engine::TickDataPtr ticker) override;

  /// 接收并打印订单数据
  asio::awaitable<void> recv_order(engine::OrderDataPtr order) override;
};

}  // namespace testing
}  // namespace strage

#endif  // __STRAGE_TESTING_TESTING_H__
