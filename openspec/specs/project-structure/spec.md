# project-structure Specification

## Purpose
TBD - created by archiving change refactor-project-structure. Update Purpose after archive.
## Requirements
### Requirement: Module Directory Organization

The system SHALL organize code into logical modules with clear separation of concerns.

#### Scenario: Top-level directory structure

- **WHEN** the project is organized
- **THEN** top-level directories SHALL represent major functional modules
- **AND** each module SHALL have a clear, single responsibility
- **AND** module names SHALL be lowercase and descriptive

#### Scenario: Module subdirectories

- **WHEN** a module contains multiple components
- **THEN** subdirectories SHALL separate base classes from implementations
- **AND** a `base/` subdirectory SHALL contain abstract interfaces and base classes
- **AND** implementation-specific subdirectories SHALL contain concrete implementations

#### Scenario: Common utilities organization

- **WHEN** shared utilities are needed across modules
- **THEN** they SHALL be placed in the `common/` directory
- **AND** utilities SHALL be further organized by category (config, utils, context)
- **AND** each category SHALL have its own subdirectory

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

