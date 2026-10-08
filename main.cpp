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
#include <boost/asio/signal_set.hpp>
#include <boost/system.hpp>
#include <boost/system/system_error.hpp>

#include "config/config.h"
#include "config/options.h"
#include "wework/wework.h"
#include "testing/testing.h"
#include "grid/grid_strategy.h"
#include "multilevel/multilevel_strategy.h"
#include "okx/okx.h"
#include "base/backtest_gateway.h"
#include "paper/paper_gateway.h"
#include "core/execution/backtest_execution_venue.h"
#include "core/execution/gateway_execution_venue_adapter.h"
#include "core/market/csv_market_data_feed.h"
#include "core/market/gateway_market_data_adapter.h"
#include "core/runtime/command_queue.h"
#include "core/runtime/strategy_runtime.h"
#include "core/runtime/strategy_runtime_component.h"

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
  if ((*AppOptions)(argc, argv) != ErrCode::OK) {
    return 1;
  }

  // 帮助信息不依赖配置文件，直接输出后退出
  if (AppOptions->is_help()) {
    AppOptions->show_help();
    return 0;
  }

  // 初始化Google日志系统
  google::InitGoogleLogging(argv[0]);
  FLAGS_minloglevel = google::INFO;  // 设置最小日志级别为INFO
  FLAGS_v = AppOptions->verbose();   // --v=N 开启 VLOG(N) 调试日志

  // 未指定 --log 时保持输出到 stderr；指定时按目录 + 文件前缀落盘。
  const std::string log_path = AppOptions->log_file();
  if (!log_path.empty()) {
    const auto separator = log_path.find_last_of('/');
    if (separator != std::string::npos) {
      FLAGS_log_dir = log_path.substr(0, separator);
      FLAGS_logtostderr = false;
      FLAGS_alsologtostderr = true;
      LOG(INFO) << fmt::format("日志文件目录: {} (文件名由 glog 按程序名生成)", FLAGS_log_dir);
    } else {
      LOG(WARNING) << fmt::format("--log 需要带目录的路径，已忽略: {}", log_path);
    }
  } else {
    FLAGS_logtostderr = true;  // 日志输出到标准错误
  }

  LOG(INFO) << "CONFIG FILE: " << AppOptions->config_file();

  // 回测模式不需要交易所配置；实盘和 Paper 模式需要加载配置文件。
  if (AppOptions->is_paper() || !AppOptions->is_backtest()) {
    try {
      AppConfig->init(AppOptions->config_file());
      // 实盘模式额外需要企业微信通知配置
      if (AppOptions->is_paper()) {
        AppConfig->load_config({okx_config, common_config});
      } else {
        AppConfig->load_config({okx_config, wework_config, common_config});
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << fmt::format("加载配置文件失败: {} ({})，"
                                "实盘与 Paper 模式需要提供可读的配置文件",
                                AppOptions->config_file(), e.what());
      return 1;
    }
  }

  // 创建异步IO上下文，用于处理所有异步操作
  boost::asio::io_context io_context;
  // 创建交易引擎，负责事件分发和组件管理
  auto engine = std::make_shared<engine::Engine>(io_context);

  dec_float initial_capital;
  try {
    initial_capital = dec_float(AppOptions->initial_capital());
    if (initial_capital <= 0) {
      LOG(ERROR) << "初始资金必须大于 0";
      return 1;
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << fmt::format("初始资金格式错误: {}", e.what());
    return 1;
  }

  // ========== 通用运行时端口准备 ==========
  // 必须在策略之前创建：行情适配器会注册引擎行情回调，
  // 先注册才能保证策略处理行情时读到的是本帧快照而不是上一帧。
  std::shared_ptr<core::execution::ExecutionVenue> venue;
  std::shared_ptr<core::market::MarketDataFeed> runtime_feed;
  std::shared_ptr<core::execution::BacktestExecutionVenue> backtest_venue;
  std::shared_ptr<core::market::CsvMarketDataFeed> csv_feed;

  // 费率与资金都是高精度解析，非法输入会抛异常，必须统一兜住。
  dec_float maker_fee_rate(0);
  dec_float taker_fee_rate(0);
  try {
    maker_fee_rate = dec_float(AppOptions->maker_fee_rate());
    taker_fee_rate = dec_float(AppOptions->taker_fee_rate());
    if (maker_fee_rate < 0 || taker_fee_rate < 0) {
      LOG(ERROR) << "手续费率不能为负数";
      return 1;
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << fmt::format("手续费率格式错误: {}", e.what());
    return 1;
  }

  const std::string venue_name = AppOptions->resolved_venue();
  if (venue_name == "backtest") {
    backtest_venue = std::make_shared<core::execution::BacktestExecutionVenue>();
    // 费率必须同时注入执行端口：统一账本根据执行回报累计成本，
    // 策略正是通过它感知手续费的。
    backtest_venue->setFeeRates(maker_fee_rate, taker_fee_rate);
    venue = backtest_venue;
    // 回测行情由网关的回放循环驱动，这里只负责 CSV 数据到标准化快照的转换。
    const std::string data_file = AppOptions->data_file();
    if (data_file.empty()) {
      LOG(ERROR) << "回测执行端口需要指定数据文件: --data-file <path>";
      return 1;
    }
    csv_feed = std::make_shared<core::market::CsvMarketDataFeed>(
        data_file, AppOptions->start_date(), AppOptions->end_date());
    if (!csv_feed->isValid()) {
      LOG(ERROR) << fmt::format("回测数据文件不可读: {}", data_file);
      return 1;
    }
    runtime_feed = csv_feed;
  } else if (venue_name == "gateway") {
    venue = std::make_shared<core::execution::GatewayExecutionVenueAdapter>(engine);
    runtime_feed = std::make_shared<core::market::GatewayMarketDataAdapter>(engine);
  } else {
    LOG(ERROR) << fmt::format("未知执行端口: {}，可选值为 auto、gateway 或 backtest",
                              AppOptions->venue());
    return 1;
  }

  // 根据 --strategy 参数创建策略组件
  std::string strategy_name = AppOptions->strategy_name();
  std::shared_ptr<engine::Component> strategy;

  if (strategy_name == "grid") {
    auto grid_upper_text = AppOptions->grid_upper();
    auto grid_lower_text = AppOptions->grid_lower();
    auto grid_amount_text = AppOptions->grid_amount();
    const int grid_count = AppOptions->grid_count();
    if (grid_upper_text.empty() || grid_lower_text.empty()) {
      LOG(ERROR) << "网格策略需要指定 --grid-upper 和 --grid-lower 参数";
      return 1;
    }

    try {
      const dec_float grid_upper(grid_upper_text);
      const dec_float grid_lower(grid_lower_text);
      const dec_float grid_amount(grid_amount_text);
      if (grid_count <= 0 || grid_upper <= grid_lower || grid_amount <= 0) {
        LOG(ERROR) << "网格参数无效：要求 upper > lower、grid-count > 0、grid-amount > 0";
        return 1;
      }

      strategy = std::make_shared<strategy::grid::GridStrategy>(
          AppOptions->symbol(), grid_upper, grid_lower, grid_count, grid_amount);
      LOG(INFO) << fmt::format("策略: 网格交易, 交易对: {}, 区间: [{}, {}], 网格数: {}, 每格: {}",
          AppOptions->symbol(), grid_lower_text, grid_upper_text, grid_count, grid_amount_text);
    } catch (const std::exception& e) {
      LOG(ERROR) << fmt::format("网格参数格式错误: {}", e.what());
      return 1;
    }
  } else if (strategy_name == "multilevel") {
    try {
      strategy::multilevel::MultiLevelConfig config;
      config.symbol = AppOptions->symbol();
      config.levels = AppOptions->mm_levels();
      config.order_budget = AppOptions->mm_order_budget();
      config.order_size = std::stod(AppOptions->mm_order_size());
      config.inventory_limit = std::stod(AppOptions->mm_inventory_limit());
      config.decision_interval_ms = AppOptions->mm_decision_interval_ms();
      config.inventory_penalty = AppOptions->mm_inventory_penalty();
      config.learning_rate = AppOptions->mm_learning_rate();
      config.exploration = AppOptions->mm_exploration();
      config.model_path = AppOptions->model_path();
      config.min_half_spread_bps = AppOptions->mm_min_half_spread_bps();
      config.allow_market_orders = AppOptions->mm_allow_market_orders() != 0;
      config.inventory_skew_bps = AppOptions->mm_inventory_skew_bps();
      if (config.levels <= 0 || config.order_budget <= 0 || config.order_size <= 0.0 ||
          config.inventory_limit <= 0.0 || config.decision_interval_ms < 0 ||
          config.learning_rate <= 0.0 || config.exploration < 0.0 ||
          config.min_half_spread_bps < 0.0 || config.inventory_skew_bps < 0.0) {
        LOG(ERROR) << "多层级做市参数无效";
        return 1;
      }
      strategy = std::make_shared<strategy::multilevel::MultiLevelMarketMakingStrategy>(
          std::move(config));
      LOG(INFO) << "策略: Multi-Level Market Making with Actor-Critic";
    } catch (const std::exception& e) {
      LOG(ERROR) << fmt::format("多层级做市参数格式错误: {}", e.what());
      return 1;
    }
  } else if (strategy_name == "testing") {
    strategy = std::make_shared<strategy::testing::Testing>(AppOptions->symbol());
    LOG(INFO) << fmt::format("策略: Testing, 交易对: {}", AppOptions->symbol());
  } else {
    LOG(ERROR) << "未知策略: " << strategy_name << "，可选值为 testing、grid 或 multilevel";
    return 1;
  }

  std::shared_ptr<core::runtime::StrategyRuntime> runtime;
  std::shared_ptr<core::runtime::StrategyRuntimeComponent> runtime_component;
  {
    auto trading_strategy = std::dynamic_pointer_cast<strategy::base::Strategy>(strategy);
    if (!trading_strategy) {
      LOG(ERROR) << "当前策略不支持通用策略运行时";
      return 1;
    }
    auto ledger = std::make_shared<core::portfolio::PortfolioLedger>(initial_capital);
    // 风控默认全为 0 等于不启用。这里给出一条始终生效的底线：
    // 单笔名义价值不得超过初始资金，其余上限仍由命令行显式开启。
    core::risk::RiskLimits limits;
    limits.max_order_notional = initial_capital;
    try {
      const auto parse_limit = [](const std::string& text) {
        return text.empty() ? dec_float(0) : dec_float(text);
      };
      limits.max_order_quantity = parse_limit(AppOptions->max_order_quantity());
      limits.max_position_quantity = parse_limit(AppOptions->max_position_quantity());
      limits.max_net_exposure = parse_limit(AppOptions->max_net_exposure());
      const std::string notional = AppOptions->max_order_notional();
      if (!notional.empty()) limits.max_order_notional = dec_float(notional);
      if (limits.max_order_quantity < 0 || limits.max_position_quantity < 0 ||
          limits.max_net_exposure < 0 || limits.max_order_notional < 0) {
        LOG(ERROR) << "风控参数无效：限额不能为负数（0 表示不启用）";
        return 1;
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << fmt::format("风控参数格式错误: {}", e.what());
      return 1;
    }
    auto risk = std::make_shared<core::risk::RiskManager>(limits);
    auto queue = std::make_shared<core::runtime::CommandQueue>(1024);
    runtime = std::make_shared<core::runtime::StrategyRuntime>(
        ledger, risk, venue, queue);
    runtime->setEngineExecutor(engine->executor());
    // 这里只能捕获弱引用：策略运行时被策略持有、策略又被引擎持有，
    // 若这里再强引用引擎就形成环，退出时引擎与组件都无法释放。
    std::weak_ptr<engine::Engine> active_engine = engine;
    runtime->setActiveChecker([active_engine]() {
      if (auto engine_ptr = active_engine.lock()) {
        return engine_ptr->is_running() && !engine_ptr->is_stopping();
      }
      return false;
    });
    runtime->setDiagnosticCallback(
        [](const core::domain::RuntimeDiagnostic& diagnostic) {
          switch (diagnostic.severity) {
            case core::domain::DiagnosticSeverity::ERROR:
              LOG(ERROR) << fmt::format("[运行时诊断] code={} plan={} order={} {}",
                                        static_cast<int>(diagnostic.code), diagnostic.plan_id,
                                        diagnostic.order_id, diagnostic.message);
              break;
            case core::domain::DiagnosticSeverity::WARNING:
              // 风控拒绝在高频做市下非常密集：明细走 --v=1，
              // 常规日志只做限流摘要，避免长期运行时日志膨胀，
              // 同时保证"策略为什么没成交"仍然可见。
              VLOG(1) << fmt::format("[运行时诊断] code={} plan={} order={} {}",
                                     static_cast<int>(diagnostic.code), diagnostic.plan_id,
                                     diagnostic.order_id, diagnostic.message);
              LOG_EVERY_N(WARNING, 50) << fmt::format(
                  "[运行时诊断] code={} plan={} order={} {}（明细见 --v=1）",
                  static_cast<int>(diagnostic.code), diagnostic.plan_id,
                  diagnostic.order_id, diagnostic.message);
              break;
            case core::domain::DiagnosticSeverity::INFO:
              LOG(INFO) << fmt::format("[运行时诊断] plan={} {}", diagnostic.plan_id,
                                       diagnostic.message);
              break;
          }
        });
    // 行情数据源接入后，策略上下文的快照由数据源驱动，策略无需自行更新行情。
    if (runtime_feed) {
      runtime->setMarketFeed(runtime_feed);
      core::domain::MarketSubscription subscription;
      subscription.symbol = AppOptions->symbol();
      subscription.tick = true;
      subscription.book = true;
      subscription.bar = true;
      runtime->subscribeMarket(subscription);
    }

    trading_strategy->set_runtime_context(runtime->context(), runtime);
    runtime_component = std::make_shared<core::runtime::StrategyRuntimeComponent>(
        engine, runtime);
    LOG(INFO) << fmt::format("通用策略运行时: 执行端口: {}, 行情源: {}",
                             venue_name, runtime_feed ? "已接入" : "未接入");
  }

  if (AppOptions->is_backtest()) {
    // ========== 回测模式 ==========
    auto data_file = AppOptions->data_file();
    if (data_file.empty()) {
      LOG(ERROR) << "回测模式需要指定数据文件: --data-file <path>";
      return 1;
    }

    auto backtest_gw = std::make_shared<backtest::base::BacktestGateway>(
        engine, data_file, AppOptions->start_date(), AppOptions->end_date(),
        initial_capital, maker_fee_rate, taker_fee_rate);

    if (backtest_venue) {
      // 复用回测网关的撮合器，保证订单簿、成交记录和绩效统计只有一份。
      backtest_venue->useMatchEngine(backtest_gw->match_engine());
    }
    if (backtest_venue || csv_feed) {
      // 用同一份回放数据同时驱动执行端口撮合和运行时行情数据源，
      // 两条链路共享同一条时间线，避免重复回放。
      backtest_gw->set_tick_sink(
          [backtest_venue, csv_feed](engine::TickDataPtr tick) {
            if (!tick) return;
            if (backtest_venue) {
              backtest_venue->onMarketPrice(tick->symbol, tick->last_price,
                                            tick->timestamp_ms);
            }
            if (csv_feed) csv_feed->pushTick(*tick);
          });
      backtest_gw->set_bar_sink(
          [backtest_venue, csv_feed](std::shared_ptr<const engine::BarData> bar) {
            if (!bar) return;
            if (backtest_venue) {
              backtest_venue->onMarketPrice(bar->symbol, bar->close_price,
                                            bar->timestamp_ms);
            }
            if (csv_feed) csv_feed->pushBar(*bar);
          });
    }

    // 组件所有权交给引擎，调用方不再保留强引用，避免退出时重复析构。
    engine->register_component(std::move(strategy));
    if (runtime_component) {
      // 在网关之前注册，保证运行时组件先完成初始化并在停止阶段参与排空。
      engine->register_component(std::move(runtime_component));
    }
    engine->register_component(std::move(backtest_gw));
  } else if (AppOptions->is_paper()) {
    // ========== 模拟交易模式（Paper Trading） ==========
    auto paper_gw = std::make_shared<market::paper::PaperGateway>(
        engine, initial_capital, AppOptions->report_interval(),
        maker_fee_rate, taker_fee_rate);

    // 开启行情录制：边训练边把带盘口的行情落盘，供离线重放。
    if (!AppOptions->record_file().empty()) {
      paper_gw->setRecordPath(AppOptions->record_file());
    }

    engine->register_component(std::move(strategy));
    if (runtime_component) {
      engine->register_component(std::move(runtime_component));
    }
    engine->register_component(std::move(paper_gw));
  } else {
    // ========== 实盘模式 ==========
    auto wework = std::make_shared<notice::wework::WeworkNotice>(engine);
    auto okx = std::make_shared<market::okx::Okx>(engine);

    engine->register_component(std::move(wework));
    engine->register_component(std::move(strategy));
    if (runtime_component) {
      engine->register_component(std::move(runtime_component));
    }
    engine->register_component(std::move(okx));
  }

  // 启动通用运行时命令执行器。
  if (runtime) {
    asio::co_spawn(io_context, runtime->run(), asio::detached);
  }

  // io_context 的停止只允许发生一次：
  // 信号处理协程和引擎运行结束回调都会走到停止流程，
  // 重复投递停止操作会让调度器在退出过程中被再次操作。
  auto stop_requested = std::make_shared<std::atomic<bool>>(false);
  auto post_stop = [&io_context, stop_requested]() {
    bool expected = false;
    if (!stop_requested->compare_exchange_strong(expected, true)) return;
    // 用 post 延迟停止 io_context，避免在当前协程栈上直接停止事件循环。
    asio::post(io_context, [&io_context]() { io_context.stop(); });
  };

  // 启动引擎协程；引擎正常结束后停止 IO 上下文，允许程序自动退出。
  asio::co_spawn(io_context, engine->run(),
      [runtime, post_stop](std::exception_ptr error) {
        if (error) {
          LOG(ERROR) << "Engine run failed";
        }
        if (runtime) runtime->stop();
        post_stop();
      });

  // 注册信号处理，捕获 SIGINT(Ctrl+C) 和 SIGTERM 实现优雅关闭。
  // 这里持有引擎的弱引用：强引用会把引擎的析构推迟到 io_context 析构期，
  // 那时 main 的局部对象已释放，引擎再析构组件就会与其余对象的析构顺序打架；
  // 捕获引用则可能在回调延迟执行时悬垂。弱引用两者兼顾。
  // 引擎与运行时都只捕获弱引用：强引用会把它们的析构推迟到 io_context 析构期，
  // 那时其它对象已释放，析构顺序会与策略的弱引用形成释放竞争。
  std::weak_ptr<engine::Engine> weak_engine = engine;
  std::weak_ptr<core::runtime::StrategyRuntime> weak_runtime = runtime;
  boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
  signals.async_wait([weak_engine, weak_runtime, &io_context, post_stop](
                         const boost::system::error_code& ec, int signal_number) {
    if (!ec) {
      LOG(INFO) << fmt::format("收到信号 {}，正在优雅关闭...", signal_number);
      asio::co_spawn(io_context,
          [weak_engine, weak_runtime, post_stop]() -> asio::awaitable<void> {
            if (auto runtime = weak_runtime.lock()) {
              co_await runtime->stopAfterIdle(200);
            }
            if (auto engine = weak_engine.lock()) co_await engine->stop();
            post_stop();
          }, asio::detached);
    }
  });

  // 运行IO事件循环，阻塞直到所有异步操作完成
  io_context.run();

  // 输出通用运行时的最终状态，确认行情与账本链路是否真正接通
  if (runtime) {
    const auto* market = runtime->context()->market();
    if (market && market->last_price > 0) {
      LOG(INFO) << fmt::format("[运行时] 最后行情快照: {} @ {}", market->symbol,
                               market->last_price.str(2, std::ios_base::fixed));
    } else {
      LOG(WARNING) << "[运行时] 未收到行情快照，行情数据源未接通";
    }
    if (const auto snapshot = runtime->context()->portfolio()) {
      // 统一账本的净值按最后行情估值，供回测绩效与账本一致性核对。
      dec_float equity = snapshot->cash;
      for (const auto& position : snapshot->positions) {
        const dec_float price = market && market->symbol == position.symbol &&
                                market->last_price > 0
                                    ? market->last_price : position.average_price;
        equity += position.quantity * price;
      }
      LOG(INFO) << fmt::format(
          "[运行时] 最终账本: 现金={}, 已实现盈亏={}, 手续费={}, 持仓品种={}",
          snapshot->cash.str(2, std::ios_base::fixed),
          snapshot->realized_pnl.str(2, std::ios_base::fixed),
          snapshot->fees.str(2, std::ios_base::fixed), snapshot->positions.size());
      LOG(INFO) << fmt::format("[运行时] 账本净值: {}", equity.str(2, std::ios_base::fixed));
    }
  }

  // 取消尚未触发的信号等待：回调只持有弱引用，取消后不会有残留操作
  // 在 io_context 析构期被销毁。注意不要用 restart()+poll() 去"跑完"残留操作，
  // 那会让已经停止的事件循环里的协程帧被再执行/再销毁一次。
  signals.cancel();

  google::ShutdownGoogleLogging();
  return 0;
}
