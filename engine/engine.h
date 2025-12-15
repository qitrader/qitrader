#ifndef __MARKET_BASE_ENGINE_H__
#define __MARKET_BASE_ENGINE_H__

#include <boost/asio/experimental/concurrent_channel.hpp>
#include <memory>
#include <string>
#include <atomic>
#include "utils/utils.h"
#include "object.h"
#include <map>
#include <set>
#include <glog/logging.h>

namespace engine {

/**
 * @brief 组件基类，所有系统组件必须继承此类
 * 
 * 组件是系统的基本构建单元，包括策略、市场网关、通知服务等。
 * 每个组件都有自己的初始化和运行逻辑。
 */
class Component {
public:
  /**
   * @brief 组件运行主逻辑，在引擎启动后被调用
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> run() = 0;
  
  /**
   * @brief 组件初始化，在run()之前被调用
   * @return asio::awaitable<void> 异步协程
   */
  virtual asio::awaitable<void> init() = 0;
};

/// 事件回调函数类型，用于处理特定类型的事件
typedef std::function<asio::awaitable<void>(EventPtr)> EventCallback;

/**
 * @brief 交易引擎核心类，负责事件分发和组件管理
 * 
 * Engine是整个系统的核心，实现了事件驱动架构。它的主要职责包括：
 * - 管理所有组件的生命周期
 * - 接收并分发各类事件（行情、订单、持仓等）
 * - 维护事件类型与回调函数的映射关系
 * - 通过并发通道实现异步事件处理
 */
class Engine {
public:
  /**
   * @brief 构造函数
   * @param ctx Boost.Asio IO上下文，用于处理异步操作
   */
  Engine(asio::io_context& ctx, size_t channel_size = 1000);
  ~Engine();

  /**
   * @brief 停止引擎，发送退出事件
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> stop();

  /**
   * @brief 检查引擎是否正在运行
   * @return bool 是否运行中
   */
  bool is_running() const { return running_.load(); }

  /**
   * @brief 引擎主运行循环
   * 
   * 执行流程：
   * 1. 初始化所有组件
   * 2. 启动所有组件的run()协程
   * 3. 进入事件循环，从通道中接收事件并分发给注册的回调函数
   * 
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> run();

  /**
   * @brief 发送事件到引擎
   * @param etype 事件类型
   * @param event 事件数据
   * @return asio::awaitable<void> 异步协程
   */
  asio::awaitable<void> on_event(EventType etype, std::shared_ptr<const BaseData> event);

  /**
   * @brief 注册事件回调函数
   * 
   * 允许组件注册特定类型事件的处理函数。
   * 当对应类型的事件发生时，注册的回调函数将被异步调用。
   * 
   * @tparam EventDataType 事件数据类型
   * @param type 事件类型
   * @param callback 回调函数，接收对应类型的事件数据
   */
  template<typename EventDataType>
  void register_callback(EventType type, std::function<asio::awaitable<void>(std::shared_ptr<const EventDataType>)> callback) {
    // 将类型化的回调函数封装为通用回调，并添加到回调列表
    callbacks_[type].push_back([callback](EventPtr event) -> asio::awaitable<void> {
      auto data = std::dynamic_pointer_cast<const EventDataType>(event->data);
      co_await callback(data);
      co_return;
    });
  }

  /**
   * @brief 注册组件到引擎
   * @param component 要注册的组件
   */
  void register_component(std::shared_ptr<Component> component);
  
private:
  /// 并发事件通道，用于在协程间传递事件，容量为1000
  boost::asio::experimental::concurrent_channel<void(boost::system::error_code, EventPtr)> channel_;
  
  /// 事件类型到回调函数列表的映射
  std::map<EventType, std::vector<EventCallback>> callbacks_;
  
  /// 所有注册的组件列表
  std::vector<std::shared_ptr<Component>> components_;

  /// 引擎运行状态标志
  std::atomic<bool> running_;
};

typedef std::shared_ptr<Engine> EnginePtr;

}  // namespace engine

#endif  // __MARKET_BASE_ENGINE_H__
