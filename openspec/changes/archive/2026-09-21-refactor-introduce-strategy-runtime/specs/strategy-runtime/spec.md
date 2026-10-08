## ADDED Requirements

### Requirement: Strategy Runtime Isolation

系统 SHALL 提供独立于具体 `Gateway` 和 `Engine` 事件细节的 `StrategyContext`。策略 SHALL 通过上下文读取标准化市场与账户快照，并通过 `OrderIntent` 或 `OrderPlan` 提交目标订单。

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

#### Scenario: Strategy submits target orders
- **WHEN** 策略需要创建、撤销或替换订单
- **THEN** 策略 SHALL 提交包含策略标签和幂等键的 `OrderPlan`
- **AND** 运行时 SHALL 将计划转换为执行端口命令

### Requirement: Order Lifecycle Management

系统 SHALL 通过 `OrderManager` 统一管理活动订单、订单 ID、撤单、替换、部分成交、拒单和执行回报。

#### Scenario: Reconcile target plan
- **WHEN** 策略提交新的目标订单计划
- **THEN** `OrderManager` SHALL 对比活动订单和目标订单
- **AND** SHALL 生成必要的撤单、新单或替换操作

#### Scenario: Partial fill update
- **WHEN** 执行端口回报部分成交
- **THEN** `OrderManager` SHALL 更新订单剩余数量
- **AND** SHALL 将标准化执行回报传给账本和策略运行时

#### Scenario: Idempotent event
- **WHEN** 同一订单计划或执行回报重复到达
- **THEN** 系统 SHALL 通过计划 ID、订单 ID 或成交 ID 去重
- **AND** SHALL 避免重复下单和重复记账

### Requirement: Unified Portfolio Ledger

系统 SHALL 提供单一权威的 `PortfolioLedger`，统一维护可用/冻结资金、持仓、冻结持仓、已实现盈亏、未实现盈亏和手续费，并生成带版本号的只读 `PortfolioSnapshot`。

#### Scenario: Execution updates ledger
- **WHEN** 账本收到成交、撤单、拒单或手续费执行回报
- **THEN** 账本 SHALL 按幂等规则更新资产状态
- **AND** SHALL 生成新的快照版本

#### Scenario: Strategy cannot mutate ledger
- **WHEN** 策略读取账户或持仓信息
- **THEN** 策略 SHALL 只能获得只读快照
- **AND** 账本写入 SHALL 只能由标准化执行回报或 `LegacyLedgerAdapter` 的单向同步触发
- **AND** `StrategyContext` SHALL NOT 暴露任何现金或持仓写入接口

### Requirement: Single Execution Path

系统 SHALL 让所有策略只通过 `OrderPlan` 下单；`StrategyContext` SHALL 是策略提交订单的唯一入口，策略不得直接发送下单或撤单事件。

#### Scenario: Strategy cannot send order events
- **WHEN** 策略需要创建、撤销或替换订单
- **THEN** 策略 SHALL 只能通过 `StrategyContext` 提交 `OrderPlan`
- **AND** 策略基类 SHALL NOT 暴露下单或撤单事件方法
- **AND** 策略 SHALL NOT 自行维护资金、持仓或订单表

#### Scenario: Strategy reads own working orders
- **WHEN** 策略需要感知自身挂单
- **THEN** `StrategyContext` SHALL 提供来自 `OrderManager` 的活动订单视图
- **AND** 策略 SHALL NOT 自行维护订单表

#### Scenario: Reduce-only intent reaches the venue
- **WHEN** 订单意图声明 `reduce_only`
- **THEN** 系统 SHALL 把该约束透传到执行端口，不得在适配层静默丢弃
- **AND** 回测下 SHALL 由统一账本的可用持仓校验保证

### Requirement: Legacy State Reconciliation

系统 SHALL 通过 `LegacyLedgerAdapter` 把 Legacy Gateway 的账户和持仓事件单向同步到统一账本，禁止双向写入。

#### Scenario: Adopt legacy initial state
- **WHEN** 统一账本尚未被任何执行回报写入
- **THEN** 适配器 SHALL 用首个 Legacy 账户和持仓事件初始化统一账本
- **AND** SHALL NOT 在账本自身开始记账后再次覆盖其状态

#### Scenario: Report final inconsistency
- **WHEN** 行情与命令都已处理完毕
- **THEN** 适配器 SHALL 比较 Legacy 最新快照与统一账本并输出差异
- **AND** SHALL NOT 在过程中比较，因为账户快照天然先于成交产生

### Requirement: Pre-Execution Risk Control

系统 SHALL 在订单计划进入 `OrderManager` 前提供可插拔风险检查，至少支持订单数量、名义价值、持仓上限和单边暴露限制。

#### Scenario: Reject unsafe plan
- **WHEN** 订单计划违反风险限制
- **THEN** 风控模块 SHALL 拒绝或裁剪违规订单
- **AND** SHALL 返回结构化拒绝原因

#### Scenario: Accept valid plan
- **WHEN** 订单计划满足风险限制
- **THEN** 风控模块 SHALL 将计划交给 `OrderManager`
- **AND** SHALL 保留策略标签和幂等键

### Requirement: Asynchronous Command Execution

系统 SHALL 通过有界异步命令队列执行新运行时产生的下单、撤单和替换命令；行情回调不得等待自身事件循环中的同步执行结果。

#### Scenario: Market callback submits command
- **WHEN** 策略在行情回调中提交订单计划
- **THEN** 运行时 SHALL 将计划放入命令队列并尽快返回行情回调
- **AND** 执行结果 SHALL 通过异步订单回报返回

#### Scenario: Queue overload
- **WHEN** 命令队列达到容量上限
- **THEN** 系统 SHALL 拒绝或合并可合并计划
- **AND** SHALL 记录队列长度、计划 ID 和拒绝原因

### Requirement: Synchronous Event Completion

引擎 SHALL 为同步事件（如回测行情回放）提供可靠的完成通知：无论发送方何时开始等待，都能收到事件处理完成的信号。

#### Scenario: Completion signal arrives before waiter
- **WHEN** 事件在发送方开始等待之前就已被事件循环处理完成
- **THEN** 发送方 SHALL 仍能立即收到完成通知
- **AND** SHALL NOT 因错过一次性唤醒信号而永久挂起

#### Scenario: Engine stopping skips completion wait
- **WHEN** 引擎已进入停止阶段
- **THEN** 同步事件发送 SHALL 立即返回
- **AND** SHALL NOT 继续等待事件回调

### Requirement: Backtest Venue Integration

回测执行端口 SHALL 复用回测网关的 `MatchEngine`，保证一次回测中只有一份订单簿、一份成交记录和一份绩效统计。

#### Scenario: Replay drives a single order book
- **WHEN** 回测网关回放行情且启用了回测执行端口
- **THEN** 执行端口 SHALL 使用网关的撮合器撮合运行时订单
- **AND** 绩效分析器 SHALL 记录运行时订单产生的成交

#### Scenario: Offline venue keeps self-contained matching
- **WHEN** 回测执行端口未接入回测网关（离线训练或强化学习环境）
- **THEN** 执行端口 SHALL 使用自带撮合器独立运行
- **AND** SHALL 保持相同的订单计划与执行回报语义
