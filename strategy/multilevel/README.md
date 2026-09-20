# 多层级做市策略（multilevel）

在买卖两侧多个价格层级上分配有限的订单预算，用 Actor-Critic 强化学习优化分配比例。
观测包含订单簿失衡、价差、短期收益、自身库存，以及变长挂单集合的聚合特征。

## 参考论文

**Multi-Level Market Making with Reinforcement Learning**

- 作者：Patrick Cheridito, Moritz Weiss
- arXiv:2608.18195 [q-fin.TR]（2026-08-18 提交）
- 主页：https://arxiv.org/2608.18195
- DOI: https://doi.org/10.48550/arXiv.2608.18195

论文要点与本实现的对应关系：

| 论文 | 实现位置 |
|---|---|
| 多个价格层级、不同规模的限价单与市价单 | `allocateLots()` 的档位分配 |
| 控制库存规模 | `MultiLevelConfig::inventory_penalty` 惩罚项 |
| 多元 Logistic-Normal 分布建模订单分配 | `actor_critic.{h,cpp}` 的 Logistic-Normal 动作 |
| deep-set 编码器聚合变长订单集合为定长表示 | `makeObservation()` 的挂单聚合特征（**当前为线性近似，尚未实现 deep-set**） |
| 基于势能的奖励塑形 | `core/environment` 的 `Reward`（pnl / 手续费 / 滑点分解） |

## 复现注意事项

以下三点是实际复现中踩出来的，会直接影响结果是否可信：

### 1. 验证环境不同

论文在三类模拟市场环境（noise traders / tactical traders / strategic traders）中验证，
价差与手续费结构由模拟设定。真实市场（如 ETH-USDT 现货）可能出现价差远小于双边手续费的
情况——实测平均价差 3.17 bps 对双边手续费 16 bps，每笔交易在数学上必亏约 12.8 bps。
此时策略的最优解退化为"不交易"，不会呈现论文中的收益。

### 2. 模型容量差距

论文用 deep-set 编码器处理变长订单集合，当前 `ActorCritic` 是线性模型
（参数为 `action_size × observation_size`，`levels=2` 时共 88 个），表达能力弱于论文。
线性模型在合成对照任务上可以学会，但在真实行情的信噪比下学不到稳定的做市策略。

### 3. 训练数据必须含真实订单簿

只有 K 线时策略会退化为合成盘口（`makeSyntheticBook`），买卖深度恒为 10、价差固定，
导致订单簿失衡等核心特征成为常量，训练无从谈起。必须用真实盘口数据。

### 4. 报价必须覆盖手续费，市价单必须关闭

这是实测中影响最大的一条：直接照搬论文的"贴着盘口挂单"在真实费率下必亏。

| 手段 | 参数 | 作用 |
|---|---|---|
| 单边报价最小偏移 | `--mm-min-half-spread-bps` | 保证往返价差 ≥ 2×偏移，覆盖双边手续费 |
| 关闭市价单 | `--mm-allow-market-orders 0` | 市价单是 taker，单方向 10 bps 已超过 maker 往返的一半 |
| 库存偏斜 | `--mm-inventory-skew-bps` | 持多头时保留价下移，把库存拉回中性，压缩期末持仓浮亏 |

同一份 5 天 ETH-USDT 真实盘口回放（`--mm-levels 2 --mm-order-size 0.01
--mm-decision-interval-ms 60000 --mm-order-budget 5`）的实测对比：

| 配置 | 最终净值 | 已实现盈亏 | 手续费 | 成交 |
|---|---|---|---|---|
| 贴盘口 + 允许市价单 | 552.85 | +3.63 | 451.18 | 10098 |
| 贴盘口 + 关闭市价单 | 760.30 | — | 234.42 | 5870 |
| 偏移 16 bps | 987.18 | +6.83 | 11.57 | 268 |
| **偏移 24 bps + 偏斜 15 + 库存上限 0.2** | **997.83** | **+2.65** | **4.81** | **119** |

把数据按 9/08–9/10 与 9/11–9/13 分段独立回放，新配置两段分别为 998.75 与 998.54
（旧配置为 873.56 / 887.89），改善在两段上一致，不是对单一行情的过拟合。

需要说明的是：收敛后的净值仍略低于本金，缺口来自**期末持仓的方向性浮亏**而非手续费
（已实现盈亏已转正）。在价差 3 bps、双边费率 16 bps 的市场结构里，继续提升要靠库存
管理或更优的费率等级，单纯调价差已接近边际。

## 训练与评估

```bash
# 1) 录制真实盘口：Paper 模式下边跑边录，产出带五档盘口的 CSV
RECORD_FILE=data/eth_usdt_l2.csv ./scripts/paper_service.sh start

# 2) 离线重放训练：一轮约 23 万步、2 分钟（在线等价需要 15 小时）
./qitrader --backtest --venue backtest --strategy multilevel \
    --symbol ETH-USDT --data-file data/eth_usdt_l2.csv \
    --mm-levels 2 --mm-order-size 0.01 --mm-decision-interval-ms 0 \
    --maker-fee-rate 0.0008 --taker-fee-rate 0.001 \
    --model-path models/offline.txt
```

说明：

- `--model-path` 非空时启动时加载、关闭时保存，训练成果可跨次累积
- 模型维度与 `--mm-levels` 绑定，`levels` 变化会因维度不匹配而拒绝加载并回退到初始权重
- 决策间隔（`--mm-decision-interval-ms`）显著影响成本：实测 5 秒间隔时毛收益为负，
  60 秒间隔时毛收益转正，但手续费仍是毛收益的十倍量级

## 文件说明

| 文件 | 内容 |
|---|---|
| `multilevel_strategy.{h,cpp}` | 策略主体：观测构造、动作分配、订单计划生成、在线学习 |
| `actor_critic.{h,cpp}` | 轻量级 Actor-Critic，含模型序列化（`save`/`load`） |
| `README.md` | 本文件 |
