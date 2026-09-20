## ADDED Requirements

### Requirement: Pluggable Trading Environment

系统 SHALL 提供可替换的 `TradingEnvironment` 接口，统一描述观测、动作执行、奖励和状态转移，并支持离线回测、Paper Trading 和实时策略运行时。

#### Scenario: Environment step
- **WHEN** 调用方提交一个动作
- **THEN** 环境 SHALL 返回包含奖励、下一观测、终止状态、时间戳和执行摘要的 `Transition`
- **AND** 环境 SHALL 保留该转移的幂等标识

#### Scenario: Environment reset
- **WHEN** 开始新的回测或训练周期
- **THEN** 环境 SHALL 重置市场、账本、订单和奖励状态
- **AND** SHALL 返回初始观测和账本快照

### Requirement: Market Making Environment

系统 SHALL 提供面向多层级做市的环境适配，支持盘口、订单流、库存、自身活动订单、手续费、滑点和库存风险特征。

#### Scenario: Multi-level action
- **WHEN** 做市策略提交包含多个价格层级的动作
- **THEN** 环境 SHALL 将动作转换为带策略标签的目标订单计划
- **AND** SHALL 应用价格、数量、库存和风险约束

#### Scenario: Reward calculation
- **WHEN** 市场推进并产生执行回报
- **THEN** 环境 SHALL 根据净值变化、库存估值、交易成本和风险惩罚计算奖励
- **AND** SHALL 生成可供在线或离线训练使用的 `Transition`

#### Scenario: Cost breakdown in reward
- **WHEN** 一步内产生成交
- **THEN** 环境 SHALL 在 `Reward` 中给出手续费和滑点的成本分解
- **AND** 手续费 SHALL 取统一账本该步的手续费增量
- **AND** 滑点 SHALL 由净持仓变化与现金变化推断的实际成交均价，与订单意图价格比较得到
- **AND** 两者 SHALL 作为 `pnl` 的分解项，不从综合奖励中重复扣除

### Requirement: Execution Environment Adapter

系统 SHALL 将实时网关、Paper 网关和回测撮合器适配到统一的行情和执行端口，策略和模型不得依赖其中任一具体实现。

#### Scenario: Backtest adapter
- **WHEN** 使用历史数据启动回测
- **THEN** 系统 SHALL 通过 `MarketDataFeed` 按时间顺序推进环境
- **AND** SHALL 通过 `ExecutionVenue` 返回标准化订单和成交回报

#### Scenario: Live adapter
- **WHEN** 使用实时交易网关运行策略
- **THEN** 系统 SHALL 将交易所数据转换为标准化市场事件
- **AND** SHALL 将策略订单计划转换为交易所订单请求
- **AND** SHALL 通过相同账本和订单管理路径处理回报
