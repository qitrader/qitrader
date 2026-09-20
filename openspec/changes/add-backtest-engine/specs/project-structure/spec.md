## MODIFIED Requirements

### Requirement: Module Directory Organization
每个功能模块 SHALL 有独立的顶级目录，使用小写字母和下划线命名，包含 `base/` 子目录存放基类和接口定义。

#### Scenario: Top-level directory structure
- **WHEN** examining the project root directory
- **THEN** the following module directories SHALL exist:
  - `common/` - 通用工具和配置
  - `engine/` - 事件引擎核心
  - `market/` - 交易所网关
  - `strategy/` - 交易策略
  - `notice/` - 通知系统
  - `backtest/` - 策略回测引擎

#### Scenario: Module subdirectories
- **WHEN** a module contains multiple implementations
- **THEN** it SHALL have a `base/` subdirectory for interfaces and base classes
- **AND** each implementation SHALL have its own subdirectory

#### Scenario: Common utilities organization
- **WHEN** utility code is shared across modules
- **THEN** it SHALL be placed in `common/` with appropriate subdirectories
- **AND** subdirectories SHALL be organized by function (`config/`, `context/`, `utils/`)

#### Scenario: Backtest module organization
- **WHEN** examining the `backtest/` directory
- **THEN** it SHALL contain the following subdirectories:
  - `base/` - 回测引擎基类、绩效分析器
  - `data/` - 数据加载器（CSV 解析等）
  - `match/` - 模拟撮合引擎
