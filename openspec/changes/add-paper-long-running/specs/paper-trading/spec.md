## ADDED Requirements

### Requirement: Periodic Account Summary

模拟交易系统 SHALL 在运行期间周期性输出账户摘要，使长期运行的效果可被直接观察。

#### Scenario: Summary is emitted on schedule
- **WHEN** 模拟交易运行且距上次摘要已超过摘要间隔（默认 60 秒，可用 `--report-interval` 调整）
- **THEN** 系统 SHALL 输出一行摘要，包含运行分钟数、净值、累计盈亏与收益率、现金、冻结额度、持仓数量与均价
- **AND** SHALL 同时包含成交笔数、下单次数、撤单次数、拒单次数、收到行情条数、重连次数

#### Scenario: Frozen balance is visible
- **WHEN** 存在尚未成交的限价挂单
- **THEN** 摘要 SHALL 展示当前冻结的资金额度
- **AND** 冻结额度 SHALL 随挂单成交或撤销而回落

### Requirement: Long-Run Performance Reporting

模拟交易系统 SHALL 复用回测的绩效分析口径，在停止时输出绩效报告，使模拟交易与回测结果可横向比较。

#### Scenario: Report on graceful shutdown
- **WHEN** 模拟交易收到停止信号并优雅退出
- **THEN** 系统 SHALL 输出绩效报告，包含总收益率、最大回撤、夏普比率、胜率与盈亏比
- **AND** SHALL 输出累计成交笔数、下单请求次数与撤单次数

#### Scenario: Equity snapshots feed the report
- **WHEN** 每次周期性摘要生成
- **THEN** 系统 SHALL 记录一条净值快照，供最大回撤与夏普比率计算使用

### Requirement: Market Data Watchdog

模拟交易系统 SHALL 检测行情连接的静默挂起并主动恢复，不得让策略在无任何报错的情况下停摆。

#### Scenario: Silent stall is detected
- **WHEN** 行情推送停滞超过停滞阈值（默认 180 秒）
- **THEN** 系统 SHALL 判定连接静默挂起并输出包含停滞时长的错误日志
- **AND** SHALL 中断当前连接以唤醒挂起的读取操作

#### Scenario: Connection is rebuilt and resubscribed
- **WHEN** 连接因看门狗中断或读取异常而失效
- **THEN** 系统 SHALL 重建行情连接
- **AND** SHALL 重新订阅此前已订阅的全部品种
- **AND** SHALL 递增重连计数，使其在摘要中可见

#### Scenario: Interrupted read returns error
- **WHEN** 连接被看门狗中断
- **THEN** 挂起的行情读取操作 SHALL 立即返回错误而非继续等待
- **AND** 该连接 SHALL 不再可用，必须由调用方重建

### Requirement: Error Storm Protection

系统 SHALL 在连接失效时限制错误重试的速率与次数，不得让读写循环以 CPU 速度产生错误日志。

#### Scenario: Loops stop when connection is voided
- **WHEN** 连接被看门狗中断或读取通道被关闭
- **THEN** 读写循环 SHALL 退出而非继续重试
- **AND** SHALL NOT 产生连续的错误日志

#### Scenario: Consecutive errors are bounded
- **WHEN** 底层读写持续失败
- **THEN** 系统 SHALL 在连续失败次数达到上限后停止循环
- **AND** 每次失败之间 SHALL 有递增的退避间隔

#### Scenario: Deliberate interrupts are not errors
- **WHEN** 失败由看门狗的主动中断（`interrupt()`）引起
- **THEN** 该次失败 SHALL NOT 计入连续失败次数，循环 SHALL 干净退出
- **AND** 连续中断 SHALL NOT 使读写循环提前判死

#### Scenario: Log volume stays bounded during failures
- **WHEN** 连接持续异常
- **THEN** 单位时间内产生的日志 SHALL 受退避间隔限制
- **AND** SHALL NOT 在数秒内达到数百 MB

### Requirement: Frozen Balance Release

模拟交易系统 SHALL 在挂单被撤销时释放其占用的冻结资金或冻结持仓。

#### Scenario: Cancel releases reserved funds
- **WHEN** 一笔限价买单被撤销
- **THEN** 系统 SHALL 按未成交数量释放对应的冻结资金
- **AND** 释放后的冻结额度 SHALL 不小于零

#### Scenario: Cancel releases reserved position
- **WHEN** 一笔限价卖单被撤销
- **THEN** 系统 SHALL 按未成交数量释放对应的冻结持仓

#### Scenario: Frozen balance does not accumulate
- **WHEN** 策略反复挂单并撤销
- **THEN** 未被挂单占用的可用额度 SHALL 保持稳定
- **AND** 下单请求 SHALL NOT 因可用额度不足而被拒绝

### Requirement: Decision Interval Enforcement

策略 SHALL 按配置的决策间隔执行决策，不得因内部时钟未推进而退化为逐行情决策。

#### Scenario: Strategy clock advances with market time
- **WHEN** 策略收到行情数据
- **THEN** 策略 SHALL 用该行情的时间戳推进自身时钟
- **AND** SHALL 仅当距上次决策已超过决策间隔时才重新决策

#### Scenario: Order flow matches the configured interval
- **WHEN** 决策间隔配置为 N 毫秒
- **THEN** 单位时间内的下单请求次数 SHALL 与 N 决定的决策频率相符
- **AND** SHALL NOT 随行情推送频率线性增长

### Requirement: Sell Capacity Uses Available Inventory

策略 SHALL 依据可用库存（净库存扣除已被挂单冻结的部分）决定卖出容量，而非总库存。

#### Scenario: Sell orders respect frozen inventory
- **WHEN** 部分库存已被未成交卖单冻结
- **THEN** 策略 SHALL 仅按未冻结部分计算卖出容量
- **AND** SHALL NOT 提交超出可用持仓的卖单

#### Scenario: Rejected sell orders stay rare
- **WHEN** 策略持续运行
- **THEN** 因可用持仓不足被风控拒绝的卖单 SHALL 保持在极低比例
- **AND** 拒单次数 SHALL 在周期性摘要中可见

### Requirement: Verbose Log Discipline

系统 SHALL 将高频交易明细日志与运行观测日志分离，保证长期运行时日志体积可控。

#### Scenario: High-frequency details are suppressed by default
- **WHEN** 系统以默认日志级别运行
- **THEN** 下单请求、挂单与撤单明细、成交明细、账户与持仓查询、风控拒绝 SHALL NOT 输出
- **AND** 仅保留摘要、订阅、错误与停止相关信息

#### Scenario: Details available on demand
- **WHEN** 以 `--v=1` 或 `GLOG_v=1` 运行
- **THEN** 上述明细日志 SHALL 全部输出，用于问题排查

#### Scenario: Log growth stays bounded
- **WHEN** 系统连续运行 24 小时
- **THEN** 日志体积 SHALL 保持在数十 MB 量级
- **AND** 日志超过 200MB 时 SHALL 自动切割

### Requirement: Graceful Shutdown Terminates

模拟交易系统 SHALL 在收到停止信号后于有限时间内结束进程，不得因等待永远不会到达的行情推送而永久挂起。

#### Scenario: Shutdown interrupts the market connection
- **WHEN** 系统收到 SIGTERM 或 SIGINT 并进入关闭流程
- **THEN** 系统 SHALL 中断行情连接，使挂起的读取操作立即返回
- **AND** SHALL 输出绩效报告后结束进程，无需外部强制终止（kill -9）

### Requirement: Connection Teardown Releases Sockets

行情连接在被弃用或中断时 SHALL 关闭底层套接字，不得把文件描述符泄漏在 CLOSE-WAIT 状态。

#### Scenario: Abandoned connection is closed
- **WHEN** 看门狗中断连接，或连接已失效并被重建
- **THEN** 底层 TCP 连接 SHALL 被关闭
- **AND** 长期运行后处于 CLOSE-WAIT 的套接字数量 SHALL NOT 持续增长

### Requirement: Trading Costs Are Modeled

模拟交易系统 SHALL 按可配置的费率对每笔成交收取手续费，不得在零成本假设下计算绩效。

#### Scenario: Fee is charged on each fill
- **WHEN** 一笔成交产生
- **THEN** 系统 SHALL 按成交金额与对应费率计算手续费
- **AND** SHALL 从现金中扣除该手续费（买入加扣、卖出减收）

#### Scenario: Maker and taker rates differ
- **WHEN** 成交来自限价挂单被吃（maker）
- **THEN** SHALL 适用 maker 费率
- **AND** 当成交来自主动吃单（taker）时 SHALL 适用 taker 费率

#### Scenario: Buy fees enter position cost
- **WHEN** 买入成交产生手续费
- **THEN** 该手续费 SHALL 计入持仓成本
- **AND** 后续卖出的已实现盈亏 SHALL 反映这部分成本

#### Scenario: Cumulative fees are observable
- **WHEN** 输出周期性摘要
- **THEN** SHALL 展示累计已支付手续费，使成本占比可评估

### Requirement: Status Reports Reflect Real Errors

`paper_service.sh status` SHALL 真实反映日志中的错误数量，不得因匹配格式错误而恒为零并掩盖故障。

#### Scenario: Error count matches log severity
- **WHEN** 管理员查看运行状态
- **THEN** ERROR 计数 SHALL 依据日志行首的等级字符统计（glog 格式为 `E<日期> ...`）
- **AND** 日志中确实存在错误时 SHALL NOT 显示为 0


