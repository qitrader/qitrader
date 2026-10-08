# Change: 添加多层级强化学习做市策略

## Why

当前项目已有回测网关和基础策略，但缺少能够利用订单簿、库存和订单流动态配置多档限价单的做市策略。需要将 `Multi-Level Market Making with Reinforcement Learning` 论文中的核心思想落地为可在回测、Paper Trading 和实盘网关上复用的策略实现。

## What Changes

- 新增多层级做市策略模块，支持多档买卖限价单、订单预算分配和库存约束。
- 新增轻量级 Logistic-Normal Actor-Critic 策略网络，不引入外部机器学习依赖。
- 新增 Deep Set 风格的变长订单集合特征聚合和订单簿失衡特征。
- 新增潜势型库存奖励塑形和在线 Actor-Critic 更新。
- 为策略增加撤单事件通道，并让回测/Paper 网关能够撤销本地挂单。
- 增加命令行参数和 `--strategy multilevel` 入口。

## Impact

- Affected specs: `project-structure`、新增 `market-making` 能力。
- Affected code: `engine/object.h`、`market/base/gateway.cpp`、`strategy/base/strategy.*`、`backtest/match/*`、`backtest/base/backtest_gateway.*`、`market/paper/*`、`strategy/multilevel/*`、`common/config/options.h`、`main.cpp`、`xmake.lua`。
- 不改变已有 `testing` 和 `grid` 策略的默认行为。
