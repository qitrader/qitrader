## ADDED Requirements

### Requirement: Member Variable Naming Convention

The system SHALL enforce consistent naming conventions for all member variables across the codebase using the `m_` prefix.

#### Scenario: Member variable declaration

- **WHEN** a class declares a member variable
- **THEN** the variable name SHALL start with `m_` prefix followed by a descriptive name in camelCase
- **AND** the naming SHALL clearly indicate the variable's purpose

#### Scenario: Member variable access in methods

- **WHEN** a member function accesses a member variable
- **THEN** the variable SHALL be referenced with its `m_` prefix
- **AND** no `this->` prefix is required unless necessary for disambiguation

#### Scenario: Member variable initialization

- **WHEN** a constructor initializes member variables
- **THEN** the initialization list SHALL use the `m_` prefixed names
- **AND** parameter names SHALL NOT use the `m_` prefix

### Requirement: Header Guard Naming Convention

The system SHALL use standard-compliant header guards that avoid reserved identifiers.

#### Scenario: Header guard format

- **WHEN** a header file is created or updated
- **THEN** the header guard SHALL follow the format `QITRADER_<PATH>_<FILE>_H_`
- **AND** the path SHALL reflect the file's location relative to project root
- **AND** all characters SHALL be uppercase with underscores separating components

#### Scenario: Header guard uniqueness

- **WHEN** multiple header files exist in the project
- **THEN** each header guard SHALL be unique
- **AND** the guard SHALL NOT start with double underscores (`__`)
- **AND** the guard SHALL NOT start with underscore followed by capital letter

#### Scenario: Header guard placement

- **WHEN** a header file contains declarations
- **THEN** the `#ifndef` guard SHALL be the first non-comment line
- **AND** the `#define` SHALL immediately follow the `#ifndef`
- **AND** the `#endif` SHALL be the last line with a comment indicating the guard name

### Requirement: Class Naming Convention

The system SHALL use PascalCase for all class names consistently.

#### Scenario: Class name format

- **WHEN** a new class is defined
- **THEN** the class name SHALL use PascalCase (e.g., `TradingEngine`, `OrderBook`)
- **AND** the name SHALL be descriptive and indicate the class's purpose
- **AND** abbreviations SHALL be avoided unless widely recognized

#### Scenario: Class name consistency

- **WHEN** a class is referenced in code or documentation
- **THEN** the exact PascalCase name SHALL be used
- **AND** no variations or typos SHALL be present

### Requirement: Function Naming Convention

The system SHALL use camelCase for all function and method names.

#### Scenario: Function name format

- **WHEN** a function or method is defined
- **THEN** the name SHALL use camelCase starting with lowercase (e.g., `calculateTotal`, `sendOrder`)
- **AND** the name SHALL be a verb or verb phrase indicating the action
- **AND** boolean-returning functions SHOULD start with `is`, `has`, or `can`

#### Scenario: Function name consistency

- **WHEN** a function is called or referenced
- **THEN** the exact camelCase name SHALL be used
- **AND** the naming SHALL be consistent across declarations and definitions

### Requirement: Constant Naming Convention

The system SHALL use SCREAMING_SNAKE_CASE for all constants and macros.

#### Scenario: Constant name format

- **WHEN** a constant or macro is defined
- **THEN** the name SHALL use all uppercase letters
- **AND** words SHALL be separated by underscores
- **AND** the name SHALL clearly indicate the constant's purpose

#### Scenario: Constant usage

- **WHEN** a constant is referenced in code
- **THEN** the SCREAMING_SNAKE_CASE name SHALL be used
- **AND** the constant SHALL be defined in an appropriate scope (namespace, class, or file)

### Requirement: Code Documentation Standards

The system SHALL maintain comprehensive Doxygen-compatible documentation for all public APIs.

#### Scenario: Class documentation

- **WHEN** a class is defined
- **THEN** a Doxygen comment block SHALL precede the class declaration
- **AND** the comment SHALL include `@brief` description
- **AND** the comment SHOULD include detailed description of the class's purpose

#### Scenario: Function documentation

- **WHEN** a public function or method is defined
- **THEN** a Doxygen comment block SHALL precede the declaration
- **AND** the comment SHALL include `@brief` description
- **AND** the comment SHALL document all parameters with `@param`
- **AND** the comment SHALL document return value with `@return` if applicable

#### Scenario: Member variable documentation

- **WHEN** a member variable is declared
- **THEN** an inline comment SHALL describe its purpose
- **AND** the comment SHALL use `///<` format for inline documentation

### Requirement: Namespace Naming Convention

The system SHALL use lowercase with underscores for namespace names, matching directory structure.

#### Scenario: Namespace definition

- **WHEN** a namespace is defined
- **THEN** the name SHALL use lowercase letters
- **AND** multiple words SHALL be separated by underscores or use nested namespaces
- **AND** the namespace SHALL reflect the module's directory structure

#### Scenario: Namespace consistency

- **WHEN** code is organized in directories
- **THEN** the namespace hierarchy SHOULD match the directory hierarchy
- **AND** related classes SHALL be grouped in the same namespace
