# Change: 重构通用策略运行时与交易执行边界

## Why

当前项目的策略直接持有 `Engine`，通过事件类型构造并发送底层 `OrderData`；策略还自行维护活动订单和部分账户状态。与此同时，`BacktestGateway`、`PaperGateway` 和实盘网关分别实现了不同版本的订单、撮合和账户处理逻辑。

这种边界使策略难以在回测、Paper Trading 和实盘之间复用，也使网格、做市和强化学习策略重复实现订单生命周期、账本同步和风险控制。当前活动变更 `add-multilevel-market-making` 已经加入了多层级做市能力，本提案负责提供更通用的运行时基础，使该策略及后续策略可以脱离具体网关。

## What Changes

- 新增 `core/` 领域层，定义标准化行情、订单意图、订单计划、执行回报、账户快照和持仓快照。
- 新增 `StrategyContext`，向策略提供只读市场/账本状态、风险检查和订单计划提交能力。
- 新增 `OrderManager`，统一处理目标订单计划、订单 ID、撤单、替换、部分成交、拒单和幂等。
- 新增单一权威 `PortfolioLedger`，统一维护可用/冻结资金、持仓、已实现盈亏、未实现盈亏和手续费。
- 新增可插拔 `RiskManager`，在订单计划进入执行端口前执行数量、名义价值、持仓和单边暴露检查。
- 新增 `ExecutionVenue` 和 `MarketDataFeed` 端口，通过 Adapter 兼容现有 `Gateway`、回测撮合器、Paper Trading 和 OKX 网关。
- 新增有界异步命令队列，禁止新运行时在行情回调中等待同步下单、撤单或替换结果。
- 新增通用 `TradingEnvironment` 和 `MarketMakingEnvironment`，为强化学习策略提供 `Observation`、`Action`、`Reward` 和 `Transition` 抽象。
- 分阶段迁移 `multilevel`、`grid` 和 `testing`，保留 Legacy 路径，默认运行行为不变。

## Scope and Compatibility

本提案采用“新增抽象、适配旧实现、逐策略迁移”的方式，不一次性重写 `Engine` 或交易所协议。第一阶段只新增兼容层和最小默认实现；后续阶段再迁移策略和回测环境。

现有 `add-multilevel-market-making` 变更作为前置能力保留；本提案不得删除其多层级做市功能，只负责将其订单和状态管理逐步接入新运行时。

## Impact

- Affected specs: 新增 `strategy-runtime`、`trading-environment`；补充 `project-structure` 的通用核心层约束。
- Affected code: `core/`、`engine/`、`strategy/base/`、`strategy/multilevel/`、`strategy/grid/`、`strategy/testing/`、`market/base/`、`market/paper/`、`backtest/`、`main.cpp`、`xmake.lua`。
- Breaking change: 新运行时 API 是新增接口；旧 `Strategy`、`Gateway` 和命令行入口在迁移期间继续可用。
- 默认行为: 不启用 Runtime 开关时，现有策略仍走 Legacy 适配路径。
