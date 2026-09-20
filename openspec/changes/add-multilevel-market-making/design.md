## Context

项目采用单线程 Boost.Asio 事件驱动架构，策略只能通过 `Strategy` 接收行情并发送订单。现有回测撮合器支持基础市价单/限价单，但没有撤单事件，也没有强化学习环境接口。

## Goals / Non-Goals

- Goals：实现论文核心的多档订单分配、库存控制、Logistic-Normal 动作和轻量级 Actor-Critic 在线训练；兼容回测、Paper Trading 和现有策略生命周期。
- Non-Goals：不引入 PyTorch/LibTorch；不声称模拟撮合等价于真实交易所；不实现完整交易所级队列深度回放和多资产训练。

## Decisions

- Decision：在 `strategy/multilevel/` 内实现纯 C++ 轻量级策略网络，使用固定维度特征和线性 Actor/Critic，避免引入新机器学习依赖。
- Decision：动作采用 `不下单 + 买方 K 档 + 卖方 K 档` 的 Logistic-Normal 比例向量，默认不主动使用市价单，以适配现有现货账户和回测撮合模型。
- Decision：每次重新配置前撤销上一次仍存活的策略订单，回测/Paper 撮合器按订单 ID 移除挂单并释放冻结资源。
- Decision：奖励使用净值变化减库存惩罚，并加入库存中间价变化的潜势项；在线更新只在获得下一时刻状态后执行。
- Decision：没有订单簿事件时，从 Tick 价格和历史价差估计合成三档盘口，使 CSV 回测仍可运行；真实 Book 事件优先使用。

## Risks / Trade-offs

- 轻量级线性网络的表达能力低于论文完整的深度 Actor-Critic → 保留清晰的策略边界，后续可替换网络实现。
- 现有回测数据通常没有队列级订单簿 → 使用固定维度的自身订单聚合和合成盘口，并在日志/文档中明确模拟限制。
- 撤单事件改变网关冻结资源行为 → 仅对新撤单通道生效，不改变旧策略下单接口。

## Migration Plan

无需迁移。默认策略仍为 `testing`；使用 `--strategy multilevel` 才启用新策略。

## Open Questions

- 实际部署前需要使用目标交易所的最小价格变动、最小下单量和手续费配置替换默认值。
