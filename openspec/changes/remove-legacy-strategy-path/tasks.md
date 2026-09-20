## 1. 策略入口

- [x] 1.1 策略基类改为 `onMarket` / `onExecution`，删除 `recv_*` 与引擎订阅 API
- [x] 1.2 运行时同步 `submit`，并在行情/成交时回调策略
- [x] 1.3 运行时组件在引擎行情事件后唤醒策略（撮合之后）

## 2. 策略实现

- [x] 2.1 网格提交完整 `OrderPlan`，用执行回报更新占用
- [x] 2.2 做市只使用 `MarketSnapshot`
- [x] 2.3 testing 去掉引擎回调

## 3. 账本与装配

- [x] 3.1 删除 `LegacyLedgerAdapter` 与 `sync*`
- [x] 3.2 `main` 统一 `subscribeMarket`，不再装配 Legacy 账本
- [x] 3.3 更新 README、smoke test 与核心测试
