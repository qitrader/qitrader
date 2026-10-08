## 1. 领域模型与兼容端口

- [x] 1.1 新增 `core/domain` 的市场、订单、执行回报、账户和持仓类型。
- [x] 1.2 新增 `OrderIntent`、`OrderPlan`、`OrderPlanDiff` 和字符串幂等键模型。
- [x] 1.3 新增不可变 `PortfolioSnapshot` 和 `StrategyStateSnapshot`。
- [x] 1.4 新增 `MarketDataFeed`、`ExecutionVenue` 和能力查询接口。
- [x] 1.5 新增 `StrategyContext`，统一暴露行情、账本、风控和计划提交。
- [x] 1.6 增加 `GatewayMarketDataAdapter`、`CsvMarketDataFeed` 和 `GatewayExecutionVenueAdapter`，不改变旧事件 API。
- [x] 1.7 新增 `LegacyLedgerAdapter`，将 Legacy 账户/持仓事件单向同步到统一账本并输出不一致日志。

## 2. 账本、风控和订单管理

- [x] 2.1 实现 `PortfolioLedger`，支持现金、冻结资金、持仓、冻结持仓、盈亏和手续费。
- [x] 2.2 实现执行回报幂等处理和账本版本号。
- [x] 2.3 实现 `RiskManager` 默认规则：数量、名义价值、持仓上限和单边暴露。
- [x] 2.4 实现 `OrderManager` 的活动订单表、订单 ID、部分成交、撤单和替换。
- [x] 2.5 实现订单计划差异计算，支持相同策略标签的目标订单收敛。
- [x] 2.6 增加订单失败、拒单、账本不一致和风险拒绝的结构化运行时诊断回调。

## 3. 异步运行时

- [x] 3.1 实现有界 `CommandQueue`。
- [x] 3.2 实现 `CommandExecutor`，将计划转为执行端口命令。
- [x] 3.3 实现计划合并、撤单优先和队列满错误。
- [x] 3.4 确保新运行时不在行情回调中使用 `on_event_sync`。
- [x] 3.5 增加 Legacy/Runtime 运行模式和日志标识。

## 4. 环境与强化学习接入

- [x] 4.1 新增 `Observation`、`Action`、`Reward`、`Transition` 和 `TradingEnvironment`。
- [x] 4.2 新增 `MarketMakingEnvironment`，将多档动作转换为 `OrderPlan`。
- [x] 4.3 将库存估值和风险惩罚纳入奖励接口；手续费和滑点字段保留扩展位。
- [x] 4.4 将 `multilevel` 的运行时状态、动作输出迁移到 `OrderPlan` 路径。
- [x] 4.5 Runtime 模式下由 `PortfolioLedger` 提供现金/库存权威状态。

## 5. 各执行环境迁移

- [x] 5.1 将 CSV/K线回放适配为 `MarketDataFeed`。
- [x] 5.2 将 `MatchEngine` 和回测账本适配为独立的 `ExecutionVenue`。
- [x] 5.3 通过通用 Gateway 执行适配器接入 Paper Trading。
- [x] 5.4 通过通用 Gateway 执行适配器接入 OKX，保留交易所扩展能力。
- [x] 5.5 让 Runtime 模式下回测、Paper 和实盘共用 `OrderManager` 和 `PortfolioLedger`。
- [x] 5.6 将 `grid`、`testing` 迁移并保留 Legacy fallback。
- [x] 5.7 将 `MarketDataFeed` 接入 `StrategyRuntime`，使策略上下文的行情快照由数据源驱动。
- [x] 5.8 回测回放同时驱动执行端口撮合与运行时行情数据源，保证只有一条时间线。

## 6. 构建、测试和验收

- [x] 6.1 更新 `xmake.lua`、include 路径和 target 源文件。
- [x] 6.2 增加领域模型、订单计划差异、订单管理和账本单元测试。
- [x] 6.3 增加风控拒绝、幂等回报、部分成交和 `REPLACE_ALL` 测试。
- [x] 6.4 增加 `multilevel` 的 Runtime/Legacy 双路径 smoke test。
- [x] 6.5 验证回测、Paper 和 OKX 编译路径。
- [x] 6.6 运行 `openspec validate --strict`、构建、静态诊断和最小回测。

## 7. 收口：移除 Legacy 执行路径

- [x] 7.1 将 `reduce_only` 从 `OrderIntent` 透传到 `engine::OrderDataItem`、OKX 下单参数与回测执行端口。
- [x] 7.2 `StrategyContext` 增加活动订单视图，替代策略自行维护的订单表。
- [x] 7.3 删除 `multilevel`、`grid`、`testing` 的 Legacy 下单分支与本地资金/持仓状态。
- [x] 7.4 移除 `--runtime` 开关，将 `--runtime-venue` 收敛为 `--venue`（`auto`/`gateway`/`backtest`）。
- [x] 7.5 删除策略基类中不再有调用方的下单与撤单事件方法。
- [x] 7.6 更新 smoke test、README 与能力规格。
