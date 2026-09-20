## Context

Runtime 已经接管下单、风控和账本，但策略仍实现六套 `recv_*` 回调。回测里 `on_tick_sync` 发生在撮合之后，这个时序必须保留：本帧先更新快照并撮合已有挂单，再让策略根据新快照下单。

## Goals / Non-Goals

- Goals: 策略源文件不包含 `engine::` 业务类型；账本单点写入；网格不再维护订单表。
- Non-Goals: 不重写 OKX/Paper 网关协议；不删除 Engine 事件通道（网关适配器仍用它）。

## Decisions

- Decision: 策略唤醒仍挂在引擎行情回调上，但转换发生在 `StrategyRuntimeComponent`，回调里只把已写入的 `MarketSnapshot` 交给 `onMarket`。
- Alternatives considered: 在 `MarketDataFeed` 回调里直接调策略。回测 sink 发生在撮合之前，会导致本帧新单被立即撮合，改变成交时序。
- Decision: 删除 `LegacyLedgerAdapter`。Paper/实盘资金以 `--initial-capital` 加执行回报为准，不再用网关账户事件回写。
- Decision: `StrategyRuntime::submit` 改为同步入队，供 `onMarket` 直接调用，避免行情回调 `co_await` 执行结果。

## Risks / Trade-offs

- 实盘真实余额与 CLI 初始资金可能不一致 → 用 `--initial-capital` 对齐，后续可用独立账户端口一次性灌入。
- 网格占用状态仍是策略决策状态，不是订单表副本 → 只通过 `ExecutionReport` 更新。
