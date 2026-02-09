# Project Context

## Purpose

qitrader is a high-performance Bitcoin trading system built with modern C++ that provides:
- Real-time market data integration from cryptocurrency exchanges
- Event-driven trading strategy execution
- Automated notification system for trading events
- Modular architecture for easy extension and customization

## Tech Stack

- **Language**: C++23 (modern C++ features)
- **Build System**: xmake
- **Compiler**: Clang (with C++23 support)
- **Async Framework**: Boost.Asio (coroutines, io_context)
- **HTTP/WebSocket**: Boost.Beast, cpphttp
- **Logging**: glog (Google Logging Library)
- **Formatting**: fmt library
- **Cryptography**: OpenSSL, CryptoPP
- **JSON**: jsoncpp, Boost.JSON
- **Async I/O**: liburing

## Project Conventions

### Code Style

- **Member Variables**: Use `m_` prefix (e.g., `m_engine`, `m_timeout`)
- **Class Names**: PascalCase (e.g., `TradingEngine`, `OrderBook`)
- **Function Names**: camelCase (e.g., `sendOrder`, `calculatePrice`)
- **Constants/Macros**: SCREAMING_SNAKE_CASE (e.g., `MAX_RETRIES`, `DEFAULT_TIMEOUT`)
- **Namespaces**: lowercase with underscores, matching directory structure
- **Header Guards**: Format `QITRADER_<PATH>_<FILE>_H_` (no leading underscores)
- **Indentation**: Follow `.clang-format` configuration
- **Documentation**: Doxygen-style comments for all public APIs

### Architecture Patterns

- **Event-Driven Architecture**: Central engine dispatches events to registered components
- **Component-Based Design**: All major subsystems (strategies, gateways, notifications) inherit from `Component` base class
- **Async/Await Pattern**: Extensive use of Boost.Asio coroutines for non-blocking operations
- **Observer Pattern**: Event callbacks registered with engine for loose coupling
- **Strategy Pattern**: Pluggable trading strategies via `Strategy` base class
- **Gateway Pattern**: Abstract `Gateway` interface for exchange integrations

### Testing Strategy

- Unit tests for individual components
- Integration tests for component interactions
- Manual testing for exchange connectivity
- Smoke tests after refactoring
- Test files should be named with `_test` suffix

### Git Workflow

- **Main Branch**: `main` - stable, production-ready code
- **Feature Branches**: `feature/<name>` - new features and enhancements
- **Refactor Branches**: `refactor/<name>` - code quality improvements
- **Bugfix Branches**: `bugfix/<name>` - bug fixes
- **Commit Messages**: Clear, descriptive messages following conventional commits
- **Pull Requests**: Required for all changes, with code review

## Domain Context

### Trading System Components

1. **Engine**: Core event dispatcher and component manager
   - Manages component lifecycle
   - Routes events between components
   - Provides async event channel

2. **Market Gateways**: Exchange interface implementations
   - Currently supports OKX exchange
   - Handles WebSocket connections for real-time data
   - Manages REST API calls for trading operations
   - Converts exchange-specific data to internal format

3. **Trading Strategies**: Algorithmic trading logic
   - Receives market data events
   - Makes trading decisions
   - Sends orders through gateways
   - Currently includes testing strategy

4. **Notification System**: Alert and messaging
   - WeWork (企业微信) integration
   - Sends trading alerts and system notifications
   - Configurable notification rules

### Key Data Structures

- **TickData**: Real-time price and volume information
- **OrderBook**: Bid/ask depth data
- **OrderData**: Order status and details
- **PositionData**: Current holdings and P&L
- **AccountData**: Balance and margin information

### Event Types

- Market data events (Tick, Book)
- Trading events (Order, Trade, Position)
- Query events (Account, Position, Order)
- Control events (Subscribe, Quit)
- Notification events (Message)

## Important Constraints

### Technical Constraints

- **Linux Only**: System designed for Linux environments
- **C++23 Required**: Uses latest C++ features (coroutines, concepts)
- **Clang Compiler**: Optimized for Clang, may not work with GCC
- **Single-threaded**: Uses async I/O instead of multi-threading
- **Real-time Requirements**: Low-latency event processing critical

### Business Constraints

- **Exchange API Limits**: Rate limiting on API calls
- **Market Hours**: Trading only during exchange operating hours
- **Risk Management**: Position limits and stop-loss requirements
- **Regulatory Compliance**: Must follow cryptocurrency trading regulations

### Security Constraints

- **API Key Security**: Keys stored in config files (not in code)
- **Config Files**: `config.ini` must not be committed to version control
- **HTTPS/WSS Only**: All exchange communication encrypted
- **Input Validation**: All external data must be validated

## External Dependencies

### Exchange APIs

- **OKX API**: Primary exchange integration
  - REST API for trading operations
  - WebSocket API for real-time data
  - Authentication via API key, secret, and passphrase

### Notification Services

- **WeWork (企业微信)**: Corporate messaging platform
  - Webhook-based message sending
  - Used for trading alerts and system notifications

### Third-Party Libraries

- **Boost**: Core async framework and utilities
- **OpenSSL**: Cryptographic operations for API authentication
- **glog**: Structured logging
- **fmt**: String formatting
- **jsoncpp**: JSON parsing for API responses

## Development Guidelines

### Before Making Changes

1. Review relevant specs in `openspec/specs/`
2. Check for pending changes in `openspec/changes/`
3. Understand the event flow and component interactions
4. Consider impact on real-time performance

### Code Review Checklist

- [ ] Follows naming conventions
- [ ] Includes Doxygen documentation
- [ ] No blocking operations in async code
- [ ] Proper error handling
- [ ] No memory leaks (use smart pointers)
- [ ] Thread-safe if accessing shared state
- [ ] Tested with real exchange data (if applicable)

### Performance Considerations

- Avoid heap allocations in hot paths
- Use move semantics for large objects
- Minimize copying of market data
- Keep event handlers lightweight
- Profile before optimizing
