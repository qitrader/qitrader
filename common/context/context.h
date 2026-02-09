#ifndef QITRADER_COMMON_CONTEXT_CONTEXT_H_
#define QITRADER_COMMON_CONTEXT_CONTEXT_H_

/**
 * @file context.h
 * @brief 协程上下文管理
 * 
 * 提供了协程的上下文管理功能，包括：
 * - 协程生命周期管理
 * - 协程层次结构（父子关系）
 * - 协程命名和跟踪
 */

#include <memory>
#include <fmt/format.h>
#include <atomic>
#include <functional>
#include <set>
#include <mutex>
#include "utils/utils.h"

namespace common::context {

/**
 * @brief 协程管理器，跟踪所有运行中的协程
 */
class CoroutineManager : public std::enable_shared_from_this<CoroutineManager> {
public:
  /**
   * @brief 添加运行中的协程
   * @param coroutine_name 协程名称
   */
  void add_running_coroutine(const std::string& coroutine_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    running_coroutines_.insert(coroutine_name);
  }

  /**
   * @brief 移除已结束的协程
   * @param coroutine_name 协程名称
   */
  void remove_running_coroutine(const std::string& coroutine_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    running_coroutines_.erase(coroutine_name);
  }

  /**
   * @brief 获取运行中的协程数量
   * @return size_t 协程数量
   */
  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_coroutines_.size();
  }

  /**
   * @brief 获取所有运行中的协程名称（用于调试）
   * @return std::set<std::string> 协程名称集合
   */
  std::set<std::string> get_running_coroutines() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_coroutines_;
  }
  
private:
  mutable std::mutex mutex_;  ///< 保护running_coroutines_的互斥锁
  std::set<std::string> running_coroutines_;  ///< 运行中的协程名称集合
};

/**
 * @brief 全局元数据，在所有协程上下文之间共享
 */
class GlobalMetaData : public std::enable_shared_from_this<GlobalMetaData> {
public:
  GlobalMetaData(std::shared_ptr<CoroutineManager> manager) : manager_(manager) {}

  friend class Context;
  
private:
  std::shared_ptr<CoroutineManager> manager_;  ///< 协程管理器
};

/**
 * @brief 协程元数据，每个协程独有
 */
class CoroutineMetaData : public std::enable_shared_from_this<CoroutineMetaData> {
public:
  CoroutineMetaData(const std::string& name) : coroutine_name_(name) {}
  
  std::atomic_int32_t child_count_{0};  ///< 子协程计数器，用于生成唯一名称
  friend class Context;
  
private:
  const std::string coroutine_name_;  ///< 协程名称
};

/**
 * @brief 协程上下文，管理协程的生命周期和层次结构
 * 
 * 每个协程都有一个上下文，包含：
 * - 全局元数据（共享）
 * - 协程元数据（独有）
 * - 父协程引用
 */
class Context : public std::enable_shared_from_this<Context> {
public:
  /**
   * @brief 构造函数
   * @param global_meta_data 全局元数据
   * @param coroutine_meta_data 协程元数据
   * @param father 父协程上下文
   */
  Context(std::shared_ptr<const GlobalMetaData> global_meta_data,
    std::shared_ptr<CoroutineMetaData> coroutine_meta_data,
    std::shared_ptr<const Context> father);

  /**
   * @brief 创建子协程上下文
   * @param child_name 子协程名称，为空则自动生成
   * @return std::shared_ptr<Context> 子协程上下文
   */
  std::shared_ptr<Context> fork(std::string child_name = "");

  /**
   * @brief 获取协程管理器
   * @return std::shared_ptr<CoroutineManager> 协程管理器
   */
  std::shared_ptr<CoroutineManager> coroutine_manager() const {
    return global_meta_data_->manager_;
  }

  /**
   * @brief 获取协程名称
   * @return const std::string 协程名称
   */
  const std::string name() const {
    return coroutine_meta_data_->coroutine_name_;
  }
  
private:
  std::shared_ptr<const GlobalMetaData> global_meta_data_;      ///< 全局元数据
  std::shared_ptr<CoroutineMetaData> coroutine_meta_data_;      ///< 协程元数据
  std::shared_ptr<const Context> father;                        ///< 父协程上下文
};

using ContextPtr = std::shared_ptr<Context>;

/**
 * @brief 启动一个分离的子协程（带名称）
 * 
 * @tparam ReturnType 协程返回类型
 * @tparam Args 协程参数类型
 * @param exec 执行器
 * @param ctx 父协程上下文
 * @param child_name 子协程名称
 * @param func 协程函数
 * @param args 协程参数
 */
template <typename ReturnType, typename ...Args>
void co_spawn_deteched(asio::any_io_executor& exec, ContextPtr ctx, const std::string& child_name, std::function<asio::awaitable<ReturnType>(ContextPtr, Args...)> func, Args &&...args) {
  // 创建子协程上下文
  auto child_ctx = ctx->fork(child_name);

  // 注册到协程管理器
  child_ctx->coroutine_manager()->add_running_coroutine(child_ctx->name());

  // 启动协程
  asio::co_spawn(exec, [child_ctx, func, args...]() mutable -> asio::awaitable<ReturnType> {
    auto& res = co_await func(child_ctx, std::forward<Args>(args)...);
    // 协程结束后从管理器中移除
    child_ctx->coroutine_manager()->remove_running_coroutine(child_ctx->name());
    co_return res;
  }, asio::detached);
}

/**
 * @brief 启动一个分离的子协程（自动命名）
 * 
 * @tparam ReturnType 协程返回类型
 * @tparam Args 协程参数类型
 * @param exec 执行器
 * @param ctx 父协程上下文
 * @param func 协程函数
 * @param args 协程参数
 */
template <typename ReturnType, typename ...Args>
void co_spawn_deteched(asio::any_io_executor& exec, ContextPtr ctx, std::function<asio::awaitable<ReturnType>(ContextPtr, Args...)> func, Args &&...args) {
  co_spawn_deteched(exec, ctx, "", func, std::forward<Args>(args)...);
}



}

#endif  // QITRADER_COMMON_CONTEXT_CONTEXT_H_
