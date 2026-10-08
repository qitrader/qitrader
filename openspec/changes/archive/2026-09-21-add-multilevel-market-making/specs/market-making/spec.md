## ADDED Requirements

### Requirement: Multi-Level Reinforcement Learning Market Making

系统 SHALL 提供一个可通过策略入口启用的多层级强化学习做市策略，该策略根据订单簿、订单流和库存状态，在多个买卖价格层级之间分配订单，并在每次重新配置前撤销仍有效的旧策略订单。

#### Scenario: Configure multiple price levels
- **WHEN** 策略收到目标交易对的 Tick 或订单簿数据并达到决策间隔
- **THEN** 策略 SHALL 生成多个价格层级的买卖限价单
- **AND** 所有新订单的总数量 SHALL 不超过配置的订单预算

#### Scenario: Inventory risk control
- **WHEN** 当前库存达到配置的上限或下限
- **THEN** 策略 SHALL 限制会进一步增加库存风险的订单方向
- **AND** 奖励 SHALL 包含库存风险惩罚

#### Scenario: Reconfigure active orders
- **WHEN** 新的动作需要替换仍处于挂单状态的策略订单
- **THEN** 策略 SHALL 发送撤单请求
- **AND** 回测和 Paper 网关 SHALL 将对应订单标记为已撤销并释放冻结资源

#### Scenario: Online policy update
- **WHEN** 策略获得前后两个决策时刻的状态和净值
- **THEN** 策略 SHALL 使用包含库存潜势塑形的奖励更新 Actor-Critic 参数

### Requirement: Market Making Configuration

系统 SHALL 通过命令行提供多层级做市策略的交易对、层级数、订单预算、决策间隔、库存限制、库存惩罚、学习率和探索强度配置，并保持未指定 `multilevel` 策略时现有策略行为不变。

#### Scenario: Enable strategy
- **WHEN** 用户使用 `--strategy multilevel`
- **THEN** 主程序 SHALL 创建多层级做市策略并使用现有 Gateway 运行

#### Scenario: Preserve default behavior
- **WHEN** 用户不指定 `--strategy multilevel`
- **THEN** 系统 SHALL 继续使用原有默认策略和参数
