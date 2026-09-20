# Change: 移除策略层 Legacy 双轨，只保留 Runtime 路径

## Why

策略仍然同时依赖 `engine::*` 事件回调和 `StrategyContext`。行情、成交、账户各走一遍旧类型，再翻译成领域对象。网格还在本地维护订单 ID 与层级持仓。这让目标订单模型无法单独成为策略的唯一入口。

## What Changes

- 策略基类只保留 `onMarket` / `onExecution`，参数为 `core::domain` 类型；删除 `recv_*` 与引擎订阅/查询 API。
- 行情由 `MarketDataFeed` 写入快照；策略唤醒由运行时组件在引擎行情事件之后触发，策略不看见 `TickData`。
- 网格改为提交完整 `OrderPlan`（`CANCEL_MISSING`），占用状态只由执行回报更新。
- 做市改为使用 `MarketSnapshot`，不再缓存 `engine::Book`。
- 删除 `LegacyLedgerAdapter` 与账本 `sync*` 接口；账本只接受标准化执行回报。
- 订阅统一由 `main` 通过 `StrategyRuntime::subscribeMarket` 完成。

## Impact

- Affected specs: `strategy-runtime`
- Affected code: `strategy/`、`core/runtime/`、`core/portfolio/`、`main.cpp`、`README.md`、`scripts/runtime_smoke_test.sh`
- Breaking: 自定义策略必须改实现 `onMarket`，不能再注册引擎回调
