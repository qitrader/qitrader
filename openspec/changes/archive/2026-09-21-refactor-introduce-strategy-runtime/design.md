## Context

项目使用 C++23、Boost.Asio 协程和单线程事件循环。当前调用链大致为：

```text
Strategy -> Engine Event -> Gateway -> Exchange/MatchEngine
```

`Engine` 负责组件生命周期和事件分发；`Gateway` 同时承担执行适配；`BacktestGateway` 还负责数据回放、撮合、账户账本和绩效分析。活动变更 `add-multilevel-market-making` 已增加撤单事件和多层级策略，但策略仍直接依赖 `Engine`、底层订单类型和本地状态。

本次重构的重点是增加稳定的领域接口和适配层，而不是立刻替换现有事件引擎。

## Goals / Non-Goals

### Goals

- 让策略只依赖交易领域模型和 `StrategyContext`；
- 让订单生命周期由 `OrderManager` 统一管理；
- 让账户与持仓由一个 `PortfolioLedger` 统一产生快照；
- 让回测、Paper Trading 和实盘通过 `ExecutionVenue` 复用策略；
- 让强化学习策略可以使用独立的环境、转移和奖励接口；
- 保持现有策略和命令行入口可用，支持逐步迁移；
- 保持单线程事件循环非阻塞。

### Non-Goals

- 不在本变更中重写 OKX API 协议实现；
- 不引入 PyTorch、LibTorch 或其他大型机器学习依赖；
- 不一次性实现多资产并行和多策略隔离；
- 不强制删除现有 `Strategy`、`Gateway` 和 `engine::OrderData` 接口；
- 不把简单 CSV 撮合模型宣称为真实交易所级撮合。

## Decisions

### Decision 1: 新增 `core/`，不把领域类型继续堆入 `engine/object.h`

新增以下模块：

```text
core/domain/
core/execution/
core/portfolio/
core/risk/
core/runtime/
core/environment/
```

`engine/object.h` 在迁移期间继续作为事件层类型；Adapter 负责在 `engine::*Data` 与领域对象之间转换。

### Decision 2: 策略使用目标状态模型

策略生成 `OrderPlan`，描述希望存在的订单集合，而不是直接调用下单/撤单事件。订单计划至少包含：

- 计划 ID 和策略标签；
- 交易对；
- 限价/市价订单意图；
- 买卖方向、价格、数量和层级；
- 过期时间和替换策略。

`OrderManager` 对比活动订单与目标计划，生成撤单、新单和替换操作。

### Decision 3: `PortfolioLedger` 是唯一写入者

所有成交、撤单、拒单、手续费和资金调整先转换为标准化执行回报，再由 `PortfolioLedger` 应用。策略只能获得不可变 `PortfolioSnapshot`，不得继续维护权威现金和库存。

迁移期间，Legacy 网关状态通过 `LegacyLedgerAdapter` 单向同步到新账本，并输出不一致日志；禁止双向写入。

### Decision 4: 运行时端口与现有 Gateway 适配

新增：

```text
MarketDataFeed
ExecutionVenue
```

现有 `Gateway` 通过 `GatewayMarketDataAdapter` 和 `GatewayExecutionVenueAdapter` 接入，回测 `MatchEngine` 和 Paper 执行器也实现相同端口。交易所特有能力通过能力查询和扩展字段表达，基础策略不依赖这些扩展。

### Decision 5: 异步命令队列解决嵌套同步事件

策略在 `recv_tick`、`recv_book` 或环境回调中只生成 `OrderPlan` 并提交到有界 `CommandQueue`。`CommandExecutor` 在事件循环中串行执行，执行结果以异步 `OrderUpdate` 返回。新运行时不调用 `on_event_sync`。

队列策略：

- 同一策略和交易对的最新可合并计划覆盖旧计划；
- 撤单命令具有更高优先级；
- 队列满时返回明确的 `QueueFull` 错误并记录原因；
- 所有命令带幂等键。

### Decision 6: RL 环境与交易运行时解耦

`TradingEnvironment` 提供：

```text
reset() -> Observation
step(Action) -> Transition
```

离线回测环境直接推进市场；实时运行时通过 `StrategyContext` 将行情和执行回报转换成环境转移。`ActorCritic` 只依赖 `Observation`、`Action` 和 `Transition`，不依赖网关或 `Engine`。

### Decision 7: 兼容开关与迁移顺序

增加 Runtime/Legacy 选择，但默认保持 Legacy。迁移顺序为：

1. 领域模型和端口；
2. 账本、风控和订单管理；
3. `multilevel`；
4. 回测和 Paper；
5. `grid`、`testing`；
6. OKX 实盘；
7. 删除不再使用的 Legacy 辅助逻辑。

## Risks / Trade-offs

- 抽象层增加代码量和数据转换 → 先实现最小端口，所有 Adapter 保留明确边界。
- 迁移期间可能有 Legacy 和 Runtime 两条路径 → 使用策略级配置开关，默认只启用一条路径并输出运行模式。
- 账本状态短期可能与旧网关不一致 → 只允许执行回报更新新账本，增加版本号、订单幂等键和一致性检查。
- 目标订单计划改变下单时序 → 初期保留串行执行器，稳定后再增加批量撤单/换单能力。
- 回测没有真实订单簿时环境信息不足 → `MarketDataFeed` 明确标记数据能力，禁止用合成盘口结果宣称真实成交性能。

## Migration Plan

1. 创建领域类型、端口和兼容 Adapter，不改变现有策略。
2. 创建 `PortfolioLedger`、`RiskManager`、`OrderManager` 的最小实现并增加单元测试。
3. 将 Gateway 的订单/成交/账户/持仓事件接入新运行时，Legacy 查询保持兼容。
4. 迁移 `multilevel` 的观察状态和 `OrderPlan` 输出，保留 Actor-Critic。
5. 将回测数据、撮合器和 Paper 执行器分别实现 `MarketDataFeed` 与 `ExecutionVenue`。
6. 迁移 `grid`、`testing` 和 OKX，并增加运行模式配置。
7. 通过 smoke test 后再删除重复的策略本地订单和账户状态代码。

## Open Questions

- 第一阶段保持单个 `qitrader` target，确认接口稳定后再拆静态库；
- 当前项目没有统一测试框架，第一阶段使用独立 C++ 测试 target 或最小可执行测试；
- 交易所不支持批量操作时，`OrderManager` 退化为串行执行并记录能力缺失；
- 是否需要将 `TradeData` 扩展手续费和流动性方向，第一阶段通过可选字段兼容。
