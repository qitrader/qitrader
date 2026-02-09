## Context

The qitrader project is a high-performance Bitcoin trading system built with modern C++ (C++23) and Boost.Asio. It uses an event-driven architecture with modular components including:
- Trading engine (event dispatcher and component manager)
- Market gateways (exchange interfaces, currently OKX)
- Trading strategies (pluggable strategy system)
- Notification system (WeWork integration)

The codebase has grown organically and now exhibits several inconsistencies in naming conventions and code organization that hinder maintainability and violate modern C++ best practices.

## Goals

1. **Establish consistent naming conventions** across the entire codebase
2. **Eliminate use of reserved identifiers** (header guards with `__` prefix)
3. **Improve code maintainability** through standardization
4. **Align with modern C++ best practices** (Google C++ Style Guide)
5. **Fix documentation errors** and improve code documentation quality

## Non-Goals

1. **Not changing any business logic** - This is purely a refactoring effort
2. **Not modifying the architecture** - Component structure remains the same
3. **Not adding new features** - Focus is on code quality improvements
4. **Not changing external APIs** - Public interfaces remain compatible where possible
5. **Not optimizing performance** - No algorithmic or performance changes

## Decisions

### Decision 1: Use `m_` Prefix for All Member Variables

**Rationale**: 
- Provides clear visual distinction between member variables and local variables
- Follows Google C++ Style Guide recommendations
- More explicit than underscore suffix (`_`)
- Prevents naming conflicts with getters/setters

**Alternatives Considered**:
- Keep `_` suffix: Less clear, can be confused with private functions
- No prefix: Requires `this->` everywhere, verbose
- Hungarian notation: Outdated and not recommended for modern C++

**Implementation**:
- Apply consistently across all classes
- Update all references in implementation files
- Use automated tools where possible to reduce errors

### Decision 2: Standard Header Guard Format

**Format**: `QITRADER_<PATH>_<FILE>_H_`

**Rationale**:
- Avoids reserved identifiers (no leading `__`)
- Includes project name to prevent conflicts
- Reflects file path for uniqueness
- Follows Google C++ Style Guide

**Example**:
```cpp
// Old: __ENGINE_OBJECT_H__
// New: QITRADER_ENGINE_OBJECT_H_
```

**Alternatives Considered**:
- `#pragma once`: Not standard C++, though widely supported
- UUID-based guards: Harder to read and maintain
- Shorter names: Higher risk of conflicts

### Decision 3: Phased Refactoring Approach

**Phases**:
1. Header guards (low risk, compile-time verification)
2. Member variables by module (isolated changes)
3. Documentation updates
4. Tooling and enforcement

**Rationale**:
- Minimizes risk by isolating changes
- Allows testing after each phase
- Easier to review and rollback if needed
- Maintains working codebase throughout

### Decision 4: Preserve Existing Architecture

**Rationale**:
- Refactoring should not mix with architectural changes
- Reduces scope and risk
- Easier to verify correctness
- Architectural improvements can be separate changes

**What Stays the Same**:
- Component-based architecture
- Event-driven design
- Namespace organization
- Class hierarchies
- Public APIs

## Technical Approach

### Header Guard Replacement

Use automated script with verification:
```bash
# For each header file:
# 1. Extract old guard name
# 2. Generate new guard name from path
# 3. Replace all occurrences
# 4. Verify compilation
```

### Member Variable Renaming

Manual approach with careful review:
1. Update class declaration
2. Update constructor initialization lists
3. Update all member function implementations
4. Update derived classes
5. Compile and test

### Verification Strategy

- Compile after each module
- Run existing tests
- Use `git diff` to review changes
- Pair review for critical components

## Risks and Trade-offs

### Risk 1: Breaking External Code

**Likelihood**: Low  
**Impact**: Medium  
**Mitigation**: 
- This appears to be a standalone project
- If used as library, provide compatibility layer
- Document breaking changes clearly

### Risk 2: Introducing Bugs During Refactoring

**Likelihood**: Medium  
**Impact**: High  
**Mitigation**:
- Comprehensive testing after each phase
- Code review for all changes
- Use compiler warnings to catch issues
- Maintain working branch throughout

### Risk 3: Incomplete Refactoring

**Likelihood**: Low  
**Impact**: Medium  
**Mitigation**:
- Comprehensive checklist in tasks.md
- Automated tools to find remaining issues
- Final grep-based verification

### Risk 4: Merge Conflicts with Ongoing Work

**Likelihood**: Medium  
**Impact**: Medium  
**Mitigation**:
- Coordinate with team before starting
- Complete refactoring in dedicated branch
- Merge during quiet period
- Communicate timeline clearly

## Migration Plan

### Phase 1: Preparation (Day 1)
- Create feature branch
- Document all files requiring changes
- Set up automated testing
- Communicate to team

### Phase 2: Header Guards (Day 1-2)
- Run automated replacement script
- Verify compilation
- Commit changes

### Phase 3: Member Variables (Day 2-4)
- Refactor by module (config, engine, market, strategy, notice)
- Test after each module
- Commit after each module

### Phase 4: Documentation (Day 4-5)
- Fix typos
- Update comments
- Update README and guides
- Commit changes

### Phase 5: Tooling (Day 5)
- Add linting configuration
- Add pre-commit hooks
- Update build scripts
- Document conventions

### Phase 6: Final Verification (Day 5)
- Full test suite
- Code review
- Merge to main branch

### Rollback Plan

If issues are discovered:
1. Identify problematic commit
2. Revert specific changes
3. Fix issues
4. Re-apply changes
5. Test again

Each module is committed separately for easy rollback.

## Open Questions

1. **Should we also refactor function parameter names for consistency?**
   - Recommendation: Yes, but as separate follow-up change
   
2. **Should we add const-correctness improvements?**
   - Recommendation: Yes, but as separate follow-up change
   
3. **Should we modernize include guards to `#pragma once`?**
   - Recommendation: No, stick with standard C++ for portability
   
4. **Should we refactor namespace naming (e.g., `market::base` vs `market_base`)?**
   - Recommendation: Keep current structure, it's already reasonable

5. **Should we add automated formatting with clang-format?**
   - Recommendation: Yes, `.clang-format` already exists, ensure it's enforced
