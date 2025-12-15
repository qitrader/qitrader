#include "engine.h"
#include "glog/logging.h"
#include <atomic>

namespace engine {

// 初始化引擎，创建容量为1000的并发事件通道
Engine::Engine(asio::io_context& ctx, size_t channel_size) 
  : channel_(ctx, channel_size), running_(false) {}

Engine::~Engine() {
  // 确保引擎已停止
  if (running_.load()) {
    LOG(WARNING) << "Engine destructor called while still running";
  }
  // 清理资源
  callbacks_.clear();
  components_.clear();
}

// 将事件发送到并发通道，由主事件循环处理
asio::awaitable<void> Engine::on_event(EventType etype, std::shared_ptr<const BaseData> event) {
  co_await channel_.async_send(boost::system::error_code(), std::make_shared<Event>(etype, event), asio::use_awaitable);
}

asio::awaitable<void> Engine::run() {
  running_.store(true);
  
  // 第一阶段：顺序初始化所有组件
  for (auto& component : components_) {
    try {
      co_await component->init();
    } catch (const std::exception& e) {
      LOG(ERROR) << fmt::format("Component init error: {}", e.what());
      running_.store(false);
      co_return;
    }
  }

  // 第二阶段：异步启动所有组件的运行协程
  for (auto& component : components_) {
    // 为每个组件启动一个独立的协程，并捕获异常防止崩溃
    asio::co_spawn(co_await asio::this_coro::executor, [component]() -> asio::awaitable<void> {
      try {
        co_await component->run();
      } catch (boost::system::system_error &e) {
        LOG(ERROR) << fmt::format("Component run error: {}", e.what());
      } catch (std::runtime_error &e) {
        LOG(ERROR) << fmt::format("Component run error: {}", e.what());
      } catch (...) {
        LOG(ERROR) << fmt::format("Component run error: unknown error");
      }
    }, asio::detached);
  }

  LOG(INFO) << "Engine started";

  // 第三阶段：进入主事件循环，从通道中接收并分发事件
  auto executor = co_await asio::this_coro::executor;
  while (running_.load()) {
    try {
      // 从通道中异步接收事件
      auto event = co_await channel_.async_receive(asio::use_awaitable);
      
      // 检查退出事件
      if (event->type == EventType::kQuit) {
        LOG(INFO) << "Received quit event, stopping engine";
        running_.store(false);
        break;
      }
      
      // 获取该事件类型对应的所有回调函数
      auto& callbacks = callbacks_[event->type];
      // 为每个回调函数启动一个独立的协程
      for (auto& callback : callbacks) {
        // 异步执行回调，并捕获异常防止单个回调失败影响整个系统
        asio::co_spawn(executor, [callback, event]() -> asio::awaitable<void> {
          try {
            co_await callback(event);
          } catch (boost::system::system_error &e) {
            LOG(ERROR) << fmt::format("Type {} callback error: {}", int(event->type), e.what());
          } catch (std::runtime_error &e) {
            LOG(ERROR) << fmt::format("Type {} callback error: {}", int(event->type), e.what());
          } catch (...) {
            LOG(ERROR) << fmt::format("Type {} callback error: unknown error", int(event->type)); 
          }
          co_return;
        }, asio::detached);
      }

      // 处理注册了kAll类型的回调，这些回调会接收所有类型的事件
      auto& all_callbacks = callbacks_[EventType::kAll];
      for (auto& callback : all_callbacks) {
        asio::co_spawn(executor, [callback, event]() -> asio::awaitable<void> {
          try {
            co_await callback(event);
          } catch (boost::system::system_error &e) {
            LOG(ERROR) << fmt::format("Type {} callback error: {}", int(event->type), e.what());
          } catch (std::runtime_error &e) {
            LOG(ERROR) << fmt::format("Type {} callback error: {}", int(event->type), e.what());
          } catch (...) {
            LOG(ERROR) << fmt::format("Type {} callback error: unknown error", int(event->type)); 
          }
        }, asio::detached);
      }
    } catch (const boost::system::system_error& e) {
      // Check if channel is closed or operation was cancelled
      // Different Boost versions may use different error codes
      std::string error_msg = e.what();
      if (error_msg.find("channel") != std::string::npos || 
          error_msg.find("closed") != std::string::npos ||
          e.code() == boost::asio::error::operation_aborted) {
        LOG(INFO) << "Event channel closed or operation aborted, stopping engine";
        break;
      }
      LOG(ERROR) << fmt::format("Event receive error: {} (code: {})", e.what(), e.code().value());
    } catch (const std::exception& e) {
      LOG(ERROR) << fmt::format("Event receive error: {}", e.what());
    } catch (...) {
      LOG(ERROR) << "Event receive error: unknown error";
    }
  }
  
  running_.store(false);
  LOG(INFO) << "Engine stopped";
}

asio::awaitable<void> Engine::stop() {
  if (!running_.load()) {
    co_return;
  }
  
  LOG(INFO) << "Stopping engine...";
  // 发送退出事件
  auto quit_event = std::make_shared<MessageData>("Engine shutdown");
  co_await on_event(EventType::kQuit, quit_event);
}

void Engine::register_component(std::shared_ptr<Component> component) {
  components_.push_back(component);
}

}

