# Change: Refactor Project Structure and Naming Conventions

## Why

The current codebase has several inconsistencies that reduce code quality and maintainability:

1. **Inconsistent member variable naming**: Mixed use of `m_` and `_` prefixes across different classes
2. **Non-standard header guards**: Using double underscores (`__`) which are reserved identifiers in C++
3. **Typos in documentation**: "Stragy" instead of "Strategy" in README.md
4. **Inconsistent naming conventions**: Not following modern C++ best practices consistently
5. **Suboptimal directory organization**: Some modules could be better organized

These issues make the codebase harder to maintain, understand, and extend. A comprehensive refactoring will establish clear conventions and improve code quality.

## What Changes

### 1. Standardize Member Variable Naming
- **BREAKING**: Unify all member variables to use `m_` prefix consistently (following Google C++ Style Guide)
- Remove inconsistent use of `_` prefix
- Affected classes: `ConfigTree`, `CommonConfig`, `Config`, `Gateway`, `Strategy`, `Engine`, and all derived classes

### 2. Fix Header Guard Macros
- **BREAKING**: Replace all header guards starting with `__` (reserved identifiers) with standard format
- New format: `QITRADER_<PATH>_<FILE>_H_` (e.g., `QITRADER_ENGINE_ENGINE_H_`)
- Affects all `.h` and `.hpp` files in the project

### 3. Fix Documentation Typos
- Correct "Stragy" to "Strategy" in README.md
- Review and fix other potential typos in comments and documentation

### 4. Improve Directory Structure
- Reorganize `common/` directory for better logical grouping
- Consider separating utilities into more specific categories
- Ensure consistent namespace usage matching directory structure

### 5. Standardize Class and Function Naming
- Ensure all class names use PascalCase consistently
- Ensure all function names use camelCase consistently
- Review and standardize constant naming (SCREAMING_SNAKE_CASE)

### 6. Enhance Code Documentation
- Add missing Doxygen comments for public APIs
- Ensure all classes have brief descriptions
- Document design decisions and architectural patterns

## Impact

### Affected Specs
- **code-style**: New capability defining C++ coding standards
- **project-structure**: New capability defining directory organization
- **build-system**: Modified to reflect any structural changes

### Affected Code
- **All header files** (`.h`, `.hpp`): Header guard changes
- **common/config/**: Member variable naming changes
- **engine/**: Member variable naming changes  
- **market/base/**: Member variable naming changes
- **strategy/base/**: Member variable naming changes
- **notice/**: Member variable naming changes
- **README.md**: Documentation fixes
- **All implementation files** (`.cpp`): References to renamed members

### Breaking Changes
- **Member variable renames**: All code accessing member variables must be updated
- **Header guard changes**: May affect external code including these headers (unlikely for this project)

### Migration Path
1. Update all header guards first (low risk, compile-time errors if issues)
2. Update member variable declarations and all references
3. Run full test suite to verify functionality
4. Update documentation to reflect new conventions
5. Add linting rules to enforce new conventions going forward

### Estimated Effort
- Header guards: ~2 hours (automated with scripts)
- Member variable refactoring: ~4-6 hours (requires careful review)
- Documentation updates: ~2 hours
- Testing and verification: ~2 hours
- **Total**: ~10-12 hours

### Risk Assessment
- **Low risk**: Changes are primarily cosmetic and will be caught at compile time
- **Medium impact**: Requires updating many files but no logic changes
- **Mitigation**: Comprehensive testing after each phase of changes
