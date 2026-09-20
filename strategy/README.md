# 策略目录

本目录存放交易策略实现。所有策略继承 `base::Strategy`，通过 `onMarket` 接收标准化行情快照，
并通过统一运行时上下文（`StrategyContext`）读取账本快照、提交订单计划（`OrderPlan`）。

## 目录结构

| 目录 | 说明 |
|---|---|
| `base/` | 策略基类，封装引擎事件回调与运行时上下文注入 |
| `testing/` | 示例策略，用于验证行情与交易链路是否连通 |
| `grid/` | 网格策略，按固定价格区间低买高卖 |
| [`multilevel/`](multilevel/README.md) | 多层级做市策略，用强化学习在多个价格档位间分配订单预算 |

各策略的详细介绍、参数与参考论文放在对应子目录中，其中 `multilevel` 有完整的
论文出处与复现注意事项，见 [`multilevel/README.md`](multilevel/README.md)。

## 新增策略

继承 `base::Strategy` 并实现 `onMarket`（成交状态可实现 `onExecution`），随后在 `main.cpp` 中按 `--strategy`
名称注册即可。策略不应自行维护资金与持仓状态，统一从运行时上下文读取账本快照。
