## ADDED Requirements

### Requirement: Backtest Data Loading
回测系统 SHALL 支持从 CSV 文件加载历史行情数据（Tick 数据和 K线数据），并按时间戳升序排列后注入事件引擎。

#### Scenario: 加载 Tick CSV 数据文件
- **WHEN** 用户通过命令行指定 `--data-file` 参数指向一个 Tick 格式的 CSV 文件
- **THEN** 系统 SHALL 逐行解析 CSV 文件，将每行转换为 `TickData` 对象
- **AND** 数据 SHALL 按 `timestamp_ms` 升序排列注入事件引擎

#### Scenario: 加载 K线 CSV 数据文件
- **WHEN** 用户通过命令行指定 `--data-file` 参数指向一个 K线格式的 CSV 文件
- **THEN** 系统 SHALL 逐行解析 CSV 文件，将每行转换为 `BarData` 对象
- **AND** 数据 SHALL 按 `timestamp_ms` 升序排列注入事件引擎

#### Scenario: 数据文件不存在
- **WHEN** 指定的 `--data-file` 路径不存在或不可读
- **THEN** 系统 SHALL 输出错误日志并终止回测

#### Scenario: 按日期范围过滤数据
- **WHEN** 用户指定 `--start-date` 和/或 `--end-date` 参数
- **THEN** 系统 SHALL 仅加载时间戳在指定范围内的数据记录

### Requirement: Backtest Gateway
回测网关 SHALL 继承 `market::base::Gateway` 基类，作为虚拟交易所网关替代实盘网关，将历史数据通过事件引擎分发给策略。

#### Scenario: 替代实盘网关
- **WHEN** 系统以回测模式启动（指定 `--backtest` 参数）
- **THEN** Engine 中注册的网关 SHALL 为 `BacktestGateway` 而非实盘网关
- **AND** 策略代码无需任何修改即可在回测模式下运行

#### Scenario: 数据回放
- **WHEN** BacktestGateway 的 `run()` 方法被调用
- **THEN** 它 SHALL 按时间顺序逐条读取历史数据
- **AND** 通过 `on_tick()` 或 `on_book()` 方法将数据注入事件引擎
- **AND** 所有数据回放完成后 SHALL 触发引擎停止

#### Scenario: 订阅请求处理
- **WHEN** 策略发送 `subscribe_tick` 或 `subscribe_book` 请求
- **THEN** BacktestGateway SHALL 记录订阅的品种（symbol）
- **AND** 仅回放已订阅品种的历史数据

### Requirement: Match Engine
模拟撮合引擎 SHALL 处理回测中的订单撮合，模拟真实交易所的成交逻辑。

#### Scenario: 市价单撮合
- **WHEN** 策略发送市价买单或卖单
- **THEN** MatchEngine SHALL 以当前最新 Tick 价格立即全量成交
- **AND** 通过事件引擎回调 `recv_order()` 通知策略成交结果

#### Scenario: 限价买单撮合
- **WHEN** 策略发送限价买单（价格为 P）
- **AND** 后续 Tick 数据的 `last_price` ≤ P
- **THEN** MatchEngine SHALL 以限价 P 全量成交
- **AND** 通过事件引擎回调通知策略

#### Scenario: 限价卖单撮合
- **WHEN** 策略发送限价卖单（价格为 P）
- **AND** 后续 Tick 数据的 `last_price` ≥ P
- **THEN** MatchEngine SHALL 以限价 P 全量成交
- **AND** 通过事件引擎回调通知策略

#### Scenario: 挂单管理
- **WHEN** 策略发送限价单但尚未满足成交条件
- **THEN** MatchEngine SHALL 将订单保存在待成交队列中
- **AND** 在每次新 Tick 数据到达时检查是否满足成交条件

### Requirement: Simulated Account Management
回测系统 SHALL 维护模拟账户，跟踪资金余额和持仓变化。

#### Scenario: 初始资金设置
- **WHEN** 回测开始时
- **THEN** 系统 SHALL 以配置文件中指定的初始资金（默认 10000 USDT）初始化模拟账户

#### Scenario: 成交后更新余额
- **WHEN** MatchEngine 完成一笔成交
- **THEN** 系统 SHALL 根据成交方向和数量更新账户余额和持仓
- **AND** 买入时扣减 USDT 余额、增加持仓数量
- **AND** 卖出时增加 USDT 余额、减少持仓数量

#### Scenario: 查询账户信息
- **WHEN** 策略调用 `on_request_account()` 
- **THEN** BacktestGateway SHALL 返回当前模拟账户的余额信息

#### Scenario: 查询持仓信息
- **WHEN** 策略调用 `on_request_position()`
- **THEN** BacktestGateway SHALL 返回当前模拟持仓信息

### Requirement: Performance Analysis
回测系统 SHALL 在回测结束后输出策略绩效分析报告。

#### Scenario: 计算总收益率
- **WHEN** 回测结束
- **THEN** 系统 SHALL 计算总收益率 = (期末净值 - 初始资金) / 初始资金
- **AND** 输出到终端日志

#### Scenario: 计算最大回撤
- **WHEN** 回测结束
- **THEN** 系统 SHALL 计算净值曲线从峰值到谷值的最大跌幅百分比
- **AND** 输出到终端日志

#### Scenario: 计算夏普比率
- **WHEN** 回测结束
- **THEN** 系统 SHALL 计算年化夏普比率 = (年化收益率 - 无风险利率) / 年化波动率
- **AND** 无风险利率默认为 0
- **AND** 输出到终端日志

#### Scenario: 计算交易统计
- **WHEN** 回测结束
- **THEN** 系统 SHALL 计算并输出：胜率、盈亏比、总交易次数、盈利次数、亏损次数

### Requirement: Backtest Command Line Interface
回测系统 SHALL 通过命令行参数控制回测模式的启动和配置。

#### Scenario: 启动回测模式
- **WHEN** 用户启动程序时指定 `--backtest` 参数
- **THEN** 系统 SHALL 以回测模式运行，使用 BacktestGateway 替代实盘网关
- **AND** 不连接任何真实交易所

#### Scenario: 指定数据文件
- **WHEN** 用户指定 `--data-file <path>` 参数
- **THEN** 系统 SHALL 从指定路径加载历史数据文件

#### Scenario: 指定回测时间范围
- **WHEN** 用户指定 `--start-date <date>` 和 `--end-date <date>` 参数
- **THEN** 系统 SHALL 仅使用指定时间范围内的历史数据进行回测
