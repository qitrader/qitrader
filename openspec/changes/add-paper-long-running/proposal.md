# Change: 模拟交易长期运行能力

## Why

模拟交易（Paper Trading）原本只适合短时验证策略能否跑通，不适合长期观察效果。实际长跑暴露了三类问题：一是静默失效——撤单未释放冻结额度导致策略在启动一分钟后就不再下单；行情连接在不报错的情况下停止推送，导致 20 小时零成交却没有任何错误日志。二是缺少观测手段——无法得知累计收益、连接是否健康、订单是否被拒。三是日志失控——高频日志 1 分钟可达 588KB，24 小时约 800MB 会撑爆磁盘。

## What Changes

- 新增周期性账户摘要：净值、盈亏、收益率、现金、冻结额度、持仓、成交笔数、下单数、撤单数、拒单数、行情条数、重连次数
- 模拟交易复用回测绩效分析器，停止时输出同口径绩效报告（总收益率、最大回撤、夏普比率、胜率、盈亏比）
- 新增行情看门狗：行情停滞超过阈值即判定连接静默挂起，主动中断并重建连接、重新订阅原品种
- 新增后台守护脚本：启动 / 停止 / 状态 / 日志跟踪 / 报告，进程意外退出后自动重启，日志按大小自动切割
- 新增 `--report-interval` 参数控制摘要间隔
- 修复：`PaperGateway::cancel_order` 撤销挂单后未释放冻结额度，导致可用额度被逐步耗尽
- 修复：策略按总库存而非可用库存（扣除冻结）挂卖单，卖单被风控大量拒绝
- 修复：`multilevel` 的 `m_current_timestamp` 从未赋值，决策间隔参数完全失效，策略退化为每个 tick 都下单
- 高频日志（下单请求、挂单/撤单明细、成交明细、账户与持仓查询、风控拒绝）改为 `VLOG(1)`，默认不输出，需要时用 `--v=1` 开启
- 修复（2026-09-07 线上长跑暴露）：看门狗的主动中断被计入连续错误，攒满 `kMaxConsecutiveErrors` 后 `read_loop` 提前判死，连接重建后无人读数据，行情永久停滞
- 修复（2026-09-07）：`interrupt()` 只关闭通道、不关底层连接，重连时 socket 泄漏在 CLOSE-WAIT
- 修复（2026-09-07）：`PaperGateway::shutdown()` 未打断 WebSocket，SIGTERM 后进程卡在退出流程，只能 kill -9
- 修复（2026-09-07）：`paper_service.sh status` 的 ERROR 统计正则不匹配 glog 行首等级格式，恒为 0，掩盖故障长达 5 天
- 修复（2026-09-07）：Paper 与回测均不计算手续费，做市策略成交流水极大，零成本假设会系统性高估绩效。现按 maker/taker 费率扣费，费率可配置（默认 OKX 现货 Lv1：0.0008 / 0.001）

## Impact

- Affected specs: `paper-trading`（新增能力）
- Affected code:
  - `market/paper/paper_gateway.{h,cpp}`：周期摘要、行情看门狗、绩效统计、冻结额度释放、运行计数
  - `market/okx/okx_ws.{h,cpp}`：新增 `interrupt()`，用于中断挂起的 `read()`
  - `strategy/multilevel/multilevel_strategy.cpp`：可用库存计算、策略时钟推进
  - `core/runtime/strategy_runtime.cpp`、`backtest/match/match_engine.cpp`、`main.cpp`：高频日志降级为 `VLOG`
  - `common/config/options.h`：新增 `--report-interval`
  - `scripts/paper_service.sh`：守护进程、状态查看、日志轮转
  - `README.md`：长期运行用法与摘要字段说明
