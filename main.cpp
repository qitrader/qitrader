/**
 * @file main.cpp
 * @brief 比特币交易系统主程序入口
 * 
 * 该程序实现了一个基于事件驱动的比特币交易系统，主要功能包括：
 * - 连接OKX交易所获取市场数据
 * - 执行交易策略
 * - 通过企业微信发送通知
 */

#include <fmt/core.h>
#include <glog/logging.h>

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/system.hpp>
#include <boost/system/system_error.hpp>

#include "config/config.h"
#include "config/options.h"
#include "wework/wework.h"
#include "testing/testing.h"
#include "okx/okx.h"

/**
 * @brief 程序主入口函数
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 程序退出码，0表示正常退出
 * 
 * 主要流程：
 * 1. 解析命令行参数
 * 2. 初始化日志系统
 * 3. 加载配置文件
 * 4. 创建引擎和各个组件（通知、策略、市场网关）
 * 5. 注册组件到引擎
 * 6. 启动异步事件循环
 */
int main(int argc, char* argv[]) {
  // 解析命令行参数
  (*AppOptions)(argc, argv);

  // 初始化Google日志系统
  google::InitGoogleLogging(argv[0]);
  FLAGS_minloglevel = google::INFO;  // 设置最小日志级别为INFO
  FLAGS_logtostderr = true;           // 日志输出到标准错误

  LOG(INFO) << "CONFIG FILE: " << AppOptions->config_file();
  // 初始化配置管理器并加载配置文件
  AppConfig->init(AppOptions->config_file());
  // 加载各模块的配置：OKX交易所、企业微信、通用配置
  AppConfig->load_config({
    okx_config,
    wework_config,
    common_config,
  });

  // 创建异步IO上下文，用于处理所有异步操作
  boost::asio::io_context io_context;
  // 创建交易引擎，负责事件分发和组件管理
  auto engine = std::make_shared<engine::Engine>(io_context);

  // 创建各个组件
  auto wework = std::make_shared<notice::wework::WeworkNotice>(engine);  // 企业微信通知组件
  auto testing = std::make_shared<strategy::testing::Testing>(engine);      // 测试策略组件
  auto okx = std::make_shared<market::okx::Okx>(engine);

  // 将所有组件注册到引擎
  engine->register_component(wework);
  engine->register_component(testing);
  engine->register_component(okx);

  // 启动引擎协程，开始处理事件
  asio::co_spawn(io_context, engine->run(), asio::detached);

  // 运行IO事件循环，阻塞直到所有异步操作完成
  io_context.run();
  google::ShutdownGoogleLogging();
  return 0;
}
