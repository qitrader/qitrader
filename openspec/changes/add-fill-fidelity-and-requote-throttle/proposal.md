# Change: 提升模拟撮合保真度并压低撤单抖动

## Why

线上 Paper 长跑暴露两个系统性偏差：

1. **撮合过于乐观**：限价单只要价格穿越就 100% 成交，没有排队、没有成交量约束、
   没有逆向选择成本。真实做市里挂单要排队，轮到你时价格往往已经朝不利方向走过一段。
   在这套乐观撮合下策略仍亏损 35%（其中 90% 是手续费），说明实盘只会更差。
2. **撤单抖动**：下单 78538 次里撤单 65478 次（83%），每 60 秒全撤全挂。根因是
   `OrderManager::reconcile` 用逐字段精确相等判定复用，而策略报价是随中价连续变化的量，
   价格末位每帧必变，于是所有档位都被判成"需换单"。实盘会额外付出 API 限额与手续费。

同时权重文件缺少配置指纹，半价差从 6bps 换到 24bps 后仍会静默加载已失效的权重。

## What Changes

- 撮合引擎新增成交保真度模型：前置排队量 + 主动成交量约束 + 可选逆向选择滑点
  （`FillModelConfig`，默认启用队列模型、滑点默认关闭；关闭即退回旧行为）
- `OrderManager::reconcile` 改为按"档位身份 + 价格相对容差"匹配，指纹只由语义内容构成
- 多层级策略报价按固定价格精度量化，并新增重新报价阈值（`--mm-requote-threshold-bps`）
- 权重文件首行写入版本与配置指纹，指纹不匹配时拒绝加载；新增 `reset()` 与
  `--mm-reset-model` / `--mm-eval-only` / `--model-load-path`
- `paper_service.sh` 的 stop 能清理未在 pid 文件中登记的守护进程

## Impact

- Affected specs: `backtest-engine`、`market-making`、`strategy-runtime`
- Affected code: `backtest/match/match_engine.*`、`core/execution/order_manager.*`、
  `strategy/multilevel/*`、`common/config/options.h`、`main.cpp`、`scripts/paper_service.sh`
- 行为变化：回测与 Paper 的成交会变稀疏（排队约束），撤单次数显著下降；
  需要重新评估策略表现，旧的在线学习权重会被判定为不兼容
