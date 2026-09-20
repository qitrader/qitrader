## MODIFIED Requirements

### Requirement: Strategy Runtime Isolation

系统 SHALL 提供独立于具体 `Gateway` 和 `Engine` 事件细节的 `StrategyContext`。策略 SHALL 通过上下文读取标准化市场与账户快照，并通过 `OrderPlan` 提交目标订单。

#### Scenario: Strategy reads unified state
- **WHEN** 策略处理市场事件
- **THEN** `StrategyContext` SHALL 提供标准化市场快照和不可变 `PortfolioSnapshot`
- **AND** 策略 SHALL 不直接修改账本或执行端口状态
- **AND** 市场快照 SHALL 由已接入的 `MarketDataFeed` 写入，策略不得自行更新

#### Scenario: Runtime provides market data feed
- **WHEN** 通用运行时启动
- **THEN** 运行时 SHALL 根据执行环境接入对应的 `MarketDataFeed`（回测为 CSV 数据源，Paper 与实盘为 Gateway 行情适配器）
- **AND** 行情适配器 SHALL 先于策略注册引擎行情回调，保证策略读到本帧快照
- **AND** 运行时 SHALL 在收尾时报告是否收到过行情快照，便于发现接线缺失

#### Scenario: Strategy is notified with domain snapshots
- **WHEN** 行情数据源完成本帧快照写入，且回测撮合已处理完已有挂单
- **THEN** 运行时 SHALL 调用策略 `onMarket(MarketSnapshot)`
- **AND** 策略源码 SHALL NOT 依赖 `engine::TickData`、`engine::Book` 或 `engine::OrderData`

#### Scenario: Strategy submits target orders
- **WHEN** 策略需要创建、撤销或替换订单
- **THEN** 策略 SHALL 提交包含策略标签和幂等键的 `OrderPlan`
- **AND** 运行时 SHALL 将计划转换为执行端口命令

### Requirement: Unified Portfolio Ledger

系统 SHALL 提供单一权威的 `PortfolioLedger`，统一维护可用/冻结资金、持仓、冻结持仓、已实现盈亏、未实现盈亏和手续费，并生成带版本号的只读 `PortfolioSnapshot`。

#### Scenario: Execution updates ledger
- **WHEN** 账本收到成交、撤单、拒单或手续费执行回报
- **THEN** 账本 SHALL 按幂等规则更新资产状态
- **AND** SHALL 生成新的快照版本

#### Scenario: Strategy cannot mutate ledger
- **WHEN** 策略读取账户或持仓信息
- **THEN** 策略 SHALL 只能获得只读快照
- **AND** 账本写入 SHALL 只能由标准化执行回报触发
- **AND** `StrategyContext` SHALL NOT 暴露任何现金或持仓写入接口

### Requirement: Single Execution Path

系统 SHALL 让所有策略只通过 `OrderPlan` 下单；`StrategyContext` SHALL 是策略提交订单的唯一入口，策略不得直接发送下单或撤单事件。

#### Scenario: Strategy cannot send order events
- **WHEN** 策略需要创建、撤销或替换订单
- **THEN** 策略 SHALL 只能通过 `StrategyContext` 提交 `OrderPlan`
- **AND** 策略基类 SHALL NOT 暴露下单、撤单、订阅或账户查询事件方法
- **AND** 策略 SHALL NOT 自行维护资金、持仓或订单表

#### Scenario: Strategy reads own working orders
- **WHEN** 策略需要感知自身挂单
- **THEN** `StrategyContext` SHALL 提供来自 `OrderManager` 的活动订单视图
- **AND** 策略 SHALL NOT 自行维护订单表

#### Scenario: Reduce-only intent reaches the venue
- **WHEN** 订单意图声明 `reduce_only`
- **THEN** 系统 SHALL 把该约束透传到执行端口，不得在适配层静默丢弃
- **AND** 回测下 SHALL 由统一账本的可用持仓校验保证

#### Scenario: Grid occupancy follows execution reports
- **WHEN** 网格策略收到买单或卖单的成交回报
- **THEN** 策略 SHALL 只根据 `ExecutionReport` 更新层级占用
- **AND** SHALL 通过完整 `OrderPlan` 收敛目标挂单，不得保存运行时订单 ID

## REMOVED Requirements

### Requirement: Legacy State Reconciliation
**Reason**: 策略与账本已只走 Runtime 路径，不再用网关账户事件校准统一账本。
**Migration**: Paper/实盘以 `--initial-capital` 初始化 `PortfolioLedger`，后续只应用 `ExecutionReport`。
