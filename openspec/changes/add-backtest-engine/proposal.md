# Change: 实现策略回测引擎（Add Backtest Engine）

## Why

当前 qitrader 仅支持实盘交易，策略开发者无法在不承担真实资金风险的情况下验证策略逻辑。策略回测是量化交易系统的核心功能之一，允许开发者使用历史行情数据模拟策略执行，评估策略表现（收益率、最大回撤、夏普比率等），从而在实盘部署前发现问题并优化策略参数。

## What Changes

- 新增 `backtest/` 模块，包含回测引擎核心组件
- 新增 `BacktestGateway`：继承 `Gateway` 基类，替代实盘网关，负责从历史数据文件加载 Tick/K线数据并注入事件引擎
- 新增 `BacktestEngine`：封装回测运行流程，管理模拟时间推进、数据回放和结果收集
- 新增 `MatchEngine`：模拟撮合引擎，处理订单匹配和成交模拟
- 新增 `PerformanceAnalyzer`：回测结果统计分析，计算收益率、最大回撤、夏普比率等指标
- 现有 `Strategy` 基类无需修改 —— 策略代码在实盘和回测之间完全复用
- 更新项目结构规格，增加 `backtest/` 模块定义
- 新增回测专用命令行参数（`--backtest`, `--data-file`, `--start-date`, `--end-date`）

## Impact

- Affected specs: `project-structure`（新增 backtest 模块目录）、`backtest-engine`（新增能力）
- Affected code:
  - 新增 `backtest/` 目录（base/, data/, match/）
  - 修改 `main.cpp`（增加回测模式入口）
  - 修改 `xmake.lua`（增加 backtest 模块编译配置）
  - 修改 `common/config/options.h`（增加回测命令行参数）
