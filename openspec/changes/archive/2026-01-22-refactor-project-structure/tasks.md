## 1. Preparation and Analysis
- [ ] 1.1 Create comprehensive list of all header files requiring guard updates
- [ ] 1.2 Create comprehensive list of all classes with member variables to rename
- [ ] 1.3 Document current naming patterns and their locations
- [ ] 1.4 Set up backup/branch for safe refactoring

## 2. Update Header Guards
- [ ] 2.1 Update all header guards in `common/` directory
- [ ] 2.2 Update all header guards in `engine/` directory
- [ ] 2.3 Update all header guards in `market/` directory
- [ ] 2.4 Update all header guards in `strategy/` directory
- [ ] 2.5 Update all header guards in `notice/` directory
- [ ] 2.6 Verify compilation after header guard changes

## 3. Standardize Member Variable Naming - Config Module
- [ ] 3.1 Update `ConfigTree` class: rename `m_prefix` and `m_ptree`
- [ ] 3.2 Update `CommonConfig` class: rename `m_timeout_ms`
- [ ] 3.3 Update `Config` class: rename `m_ptree`
- [ ] 3.4 Update `Options` class: rename `m_desc` and related members
- [ ] 3.5 Update all references in implementation files
- [ ] 3.6 Verify compilation and basic functionality

## 4. Standardize Member Variable Naming - Engine Module
- [ ] 4.1 Update `Engine` class: rename `running_` to `m_running`
- [ ] 4.2 Update `Engine` class: rename `channel_` to `m_channel`
- [ ] 4.3 Update `Engine` class: rename `callbacks_` to `m_callbacks`
- [ ] 4.4 Update `Engine` class: rename `components_` to `m_components`
- [ ] 4.5 Update all references in `engine.cpp`
- [ ] 4.6 Verify compilation and basic functionality

## 5. Standardize Member Variable Naming - Market Module
- [ ] 5.1 Update `Gateway` class: rename `_name` to `m_name`
- [ ] 5.2 Update `Gateway` class: rename `_engine` to `m_engine`
- [ ] 5.3 Update all references in `gateway.cpp`
- [ ] 5.4 Update derived classes (OKX gateway)
- [ ] 5.5 Update OKX-specific member variables
- [ ] 5.6 Verify compilation and basic functionality

## 6. Standardize Member Variable Naming - Strategy Module
- [ ] 6.1 Update `Strategy` class: rename `_engine` to `m_engine`
- [ ] 6.2 Update all references in `strategy.cpp`
- [ ] 6.3 Update derived classes (Testing strategy)
- [ ] 6.4 Verify compilation and basic functionality

## 7. Standardize Member Variable Naming - Notice Module
- [ ] 7.1 Update `Notice` base class member variables
- [ ] 7.2 Update `WeworkNotice` class member variables
- [ ] 7.3 Update `WeworkConfig` class: rename `m_key` and `m_uri`
- [ ] 7.4 Update all references in implementation files
- [ ] 7.5 Verify compilation and basic functionality

## 8. Fix Documentation and Comments
- [ ] 8.1 Fix "Stragy" typo in README.md (line 154)
- [ ] 8.2 Review and update all Doxygen comments for consistency
- [ ] 8.3 Add missing documentation for public APIs
- [ ] 8.4 Update code examples in documentation

## 9. Update Build Configuration
- [ ] 9.1 Review `xmake.lua` for any path-dependent configurations
- [ ] 9.2 Update `.clang-format` if needed for new conventions
- [ ] 9.3 Verify build system works with all changes

## 10. Testing and Validation
- [ ] 10.1 Run full compilation in debug mode
- [ ] 10.2 Run full compilation in release mode
- [ ] 10.3 Execute all existing tests
- [ ] 10.4 Perform manual smoke testing of core functionality
- [ ] 10.5 Verify no runtime behavior changes

## 11. Documentation Updates
- [ ] 11.1 Update README.md with new coding conventions
- [ ] 11.2 Create or update CONTRIBUTING.md with style guide
- [ ] 11.3 Document the refactoring changes in CHANGELOG
- [ ] 11.4 Update any developer documentation

## 12. Code Quality Improvements
- [ ] 12.1 Add clang-tidy configuration for enforcing conventions
- [ ] 12.2 Add pre-commit hooks for style checking
- [ ] 12.3 Document naming conventions in project.md
- [ ] 12.4 Create style guide reference document

## Dependencies
- Tasks 3-7 depend on task 2 (header guards should be done first)
- Task 9 depends on tasks 2-7 (build config after code changes)
- Task 10 depends on tasks 2-9 (testing after all changes)
- Task 11 depends on task 10 (documentation after verification)
- Task 12 can be done in parallel with task 11

## Notes
- Each module should be refactored and tested independently
- Commit after each major module completion for easy rollback
- Use search-and-replace carefully to avoid breaking string literals
- Pay special attention to template code and macros
