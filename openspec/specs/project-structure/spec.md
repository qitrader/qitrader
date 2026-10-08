# project-structure Specification

## Purpose
TBD - created by archiving change refactor-project-structure. Update Purpose after archive.
## Requirements
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

### Requirement: Header and Implementation File Placement

The system SHALL maintain consistent placement of header and implementation files.

#### Scenario: Header file location

- **WHEN** a class or interface is defined
- **THEN** the header file SHALL be placed in the same directory as its implementation
- **AND** the header file SHALL use `.h` extension for C++ headers
- **AND** template-heavy headers MAY use `.hpp` extension

#### Scenario: Implementation file location

- **WHEN** a class is implemented
- **THEN** the implementation file SHALL be in the same directory as the header
- **AND** the implementation file SHALL use `.cpp` extension
- **AND** the filename SHALL match the header filename

#### Scenario: Header-only libraries

- **WHEN** a component is header-only (templates, inline functions)
- **THEN** it MAY use `.hpp` extension to indicate header-only nature
- **AND** it SHALL be placed in the appropriate module directory

### Requirement: Module Dependencies

The system SHALL maintain clear and minimal dependencies between modules.

#### Scenario: Dependency direction

- **WHEN** modules depend on each other
- **THEN** dependencies SHALL flow from specific to general
- **AND** higher-level modules MAY depend on lower-level modules
- **AND** lower-level modules SHALL NOT depend on higher-level modules

#### Scenario: Common module dependencies

- **WHEN** any module needs shared functionality
- **THEN** it MAY depend on the `common/` module
- **AND** the `common/` module SHALL NOT depend on specific modules
- **AND** circular dependencies SHALL be avoided

#### Scenario: Engine module as core

- **WHEN** components need to interact with the system
- **THEN** they SHALL depend on the `engine/` module
- **AND** the engine SHALL provide the event system and component interfaces
- **AND** specific implementations SHALL NOT be in the engine module

### Requirement: File Naming Conventions

The system SHALL use consistent file naming conventions across the project.

#### Scenario: Source file naming

- **WHEN** a file is created
- **THEN** the filename SHALL use lowercase letters
- **AND** multiple words SHALL be separated by underscores
- **AND** the filename SHALL clearly indicate the file's contents

#### Scenario: Header-implementation pairing

- **WHEN** a class has both header and implementation
- **THEN** both files SHALL have the same base name
- **AND** only the extension SHALL differ (`.h` vs `.cpp`)
- **AND** the class name SHALL be derivable from the filename

#### Scenario: Test file naming

- **WHEN** test files are created
- **THEN** they SHALL be named after the file they test with `_test` suffix
- **AND** they SHALL be placed in a `tests/` subdirectory or alongside source
- **AND** the naming SHALL make the test target obvious

### Requirement: Include Path Organization

The system SHALL organize include paths to reflect module structure.

#### Scenario: Include statement format

- **WHEN** a file includes another header
- **THEN** the include path SHALL be relative to the project root
- **AND** the path SHALL include the module directory
- **AND** system headers SHALL use angle brackets `<>`
- **AND** project headers SHALL use quotes `""`

#### Scenario: Include order

- **WHEN** multiple headers are included
- **THEN** the corresponding header SHALL be included first (in .cpp files)
- **AND** project headers SHALL be grouped together
- **AND** third-party library headers SHALL be grouped together
- **AND** system headers SHALL be grouped together
- **AND** groups SHALL be separated by blank lines

### Requirement: Build Artifact Organization

The system SHALL separate build artifacts from source code.

#### Scenario: Build directory

- **WHEN** the project is built
- **THEN** all build artifacts SHALL be placed in a `build/` directory
- **AND** the build directory SHALL NOT be committed to version control
- **AND** the build directory SHALL be easily cleanable

#### Scenario: Binary output location

- **WHEN** executables or libraries are built
- **THEN** they SHALL be placed in appropriate subdirectories of `build/`
- **AND** debug and release builds MAY use separate subdirectories
- **AND** the output location SHALL be configurable via build system

### Requirement: Configuration File Organization

The system SHALL organize configuration files logically.

#### Scenario: Build configuration

- **WHEN** build configuration is needed
- **THEN** build files SHALL be at the project root (e.g., `xmake.lua`)
- **AND** build configuration SHALL be version controlled
- **AND** build files SHALL be named according to the build system convention

#### Scenario: Runtime configuration

- **WHEN** runtime configuration is needed
- **THEN** example configuration files SHALL be provided (e.g., `config.example.ini`)
- **AND** actual configuration files SHALL NOT be committed (e.g., `config.ini`)
- **AND** configuration file format SHALL be documented

#### Scenario: Development tool configuration

- **WHEN** development tools require configuration
- **THEN** tool configuration files SHALL be at project root (e.g., `.clang-format`)
- **AND** tool configurations SHALL be version controlled
- **AND** tool configurations SHALL enforce project standards

