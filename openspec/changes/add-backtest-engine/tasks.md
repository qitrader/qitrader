## 1. 基础设施

- [x] 1.1 创建 `backtest/` 目录结构（`base/`、`data/`、`match/`）
- [x] 1.2 更新 `xmake.lua`，添加 `backtest/` 模块的编译配置和 include 路径
- [x] 1.3 更新 `common/config/options.h`，添加回测命令行参数（`--backtest`、`--data-file`、`--start-date`、`--end-date`）

## 2. 数据加载

- [x] 2.1 实现 `backtest/data/csv_loader.h/.cpp`：CSV 文件解析器，支持 Tick 和 K线数据格式，流式逐行读取，按日期范围过滤

## 3. 模拟撮合引擎

- [x] 3.1 实现 `backtest/match/match_engine.h/.cpp`：模拟撮合引擎，支持市价单即时成交、限价单条件触发成交、挂单队列管理

## 4. 回测网关

- [x] 4.1 实现 `backtest/base/backtest_gateway.h/.cpp`：继承 `Gateway` 基类，实现数据回放（`run()` 中按时间顺序注入事件）、订阅管理、模拟账户/持仓维护、集成 MatchEngine 处理订单

## 5. 绩效分析

- [x] 5.1 实现 `backtest/base/performance_analyzer.h/.cpp`：收集交易记录和净值曲线，计算核心指标（总收益率、最大回撤、夏普比率、胜率、盈亏比），格式化输出回测报告到终端

## 6. 主程序集成

- [x] 6.1 修改 `main.cpp`：根据 `--backtest` 参数判断运行模式，回测模式下注册 `BacktestGateway` 替代实盘网关，回测结束后调用 `PerformanceAnalyzer` 输出报告
