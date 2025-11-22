#ifndef BITCOINTRADER_MARKET_BASE_OBJECT_H_
#define BITCOINTRADER_MARKET_BASE_OBJECT_H_

/**
 * @file object.h
 * @brief 定义交易系统中的所有数据结构和事件类型
 *
 * 包括：
 * - 基础数据类型（BaseData）
 * - 事件类型枚举（EventType）
 * - 市场数据（Tick, Book, Bar）
 * - 交易数据（Order, Trade, Position, Account）
 */

#include <memory>
#include <string>

#include "utils/utils.h"

namespace engine {

/**
 * @brief 所有数据类型的基类
 *
 * 包含所有数据对象的通用字段：交易对、交易所、时间戳
 */
class BaseData : public std::enable_shared_from_this<BaseData> {
 public:
  virtual ~BaseData() = default;

  std::string symbol;    ///< 交易对符号，如"BTC-USDT"
  std::string exchange;  ///< 交易所名称，如"okx"
  int64_t timestamp_ms;  ///< 时间戳（毫秒）
};

/**
 * @brief 事件类型枚举
 *
 * 定义了系统中所有可能的事件类型，用于事件路由和分发。
 */
enum class EventType {
  kQuit,  ///< 退出事件

  kSubscribeTick,  ///< 订阅Tick数据请求
  kTick,           ///< Tick数据事件

  kSubscribeBook,  ///< 订阅订单簿请求
  kBook,           ///< 订单簿数据事件

  kSendOrder,   ///< 发送订单请求
  kQueryOrder,  ///< 查询订单请求
  kOrder,       ///< 订单数据事件

  kTrade,  ///< 成交数据事件

  kQueryPosition,  ///< 查询持仓请求
  kPosition,       ///< 持仓数据事件

  kQueryAccount,  ///< 查询账户请求
  kAccount,       ///< 账户数据事件

  kMessage,  ///< 通用消息事件

  kAll,  ///< 特殊类型，表示接收所有类型的事件
};

/**
 * @brief 事件对象，封装事件类型和事件数据
 */
class Event : public std::enable_shared_from_this<Event> {
 public:
  Event(EventType type, std::shared_ptr<const BaseData> data) {
    this->type = type;
    this->data = data;
  }
  EventType type;                        ///< 事件类型
  std::shared_ptr<const BaseData> data;  ///< 事件数据
};

typedef std::shared_ptr<const Event> EventPtr;

class TickData;
typedef std::shared_ptr<const TickData> TickDataPtr;

/**
 * @brief 订单簿单项，表示买卖盘中的一个价格档位
 */
class BookItem : public BaseData {
 public:
  dec_float price;   ///< 价格
  dec_float volume;  ///< 数量
};

/**
 * @brief 订单簿数据，包含买盘和卖盘
 */
class Book : public BaseData {
 public:
  std::vector<BookItem> bids;  ///< 买盘列表，按价格从高到低排序
  std::vector<BookItem> asks;  ///< 卖盘列表，按价格从低到高排序

  const static EventType type = EventType::kBook;
};

typedef std::shared_ptr<const Book> BookPtr;

/**
 * @brief Tick数据，包含实时价格和成交信息
 */
class TickData : public BaseData {
 public:
  dec_float last_price;   ///< 最新成交价
  dec_float last_volume;  ///< 最新成交量
  dec_float turnover;     ///< 成交额

  dec_float open_price;        ///< 24小时开盘价
  dec_float high_price;        ///< 24小时最高价
  dec_float low_price;         ///< 24小时最低价
  dec_float last_close_price;  ///< 昨日收盘价

  BookPtr order_book;  ///< 关联的订单簿数据
  const static EventType type = EventType::kTick;
};

/**
 * @brief K线数据（Bar数据）
 */
class BarData : public BaseData {
 public:
  int64_t interval;  ///< K线周期（秒）

  dec_float volume;  ///< 成交量

  dec_float open_price;   ///< 开盘价
  dec_float high_price;   ///< 最高价
  dec_float low_price;    ///< 最低价
  dec_float close_price;  ///< 收盘价
};

/**
 * @brief 交易方向
 */
enum class Direction { BUY, SELL };  ///< 买入/卖出

/**
 * @brief 订单状态
 */
enum class OrderStatus {
  SUBMITTING,      ///< 订单提交中
  PENDING,         ///< 订单待成交
  PARTIAL_FILLED,  ///< 订单部分成交
  FILLED,          ///< 订单已完全成交
  CANCELLED,       ///< 订单已取消
  REJECTED,        ///< 订单被拒绝
};

/**
 * @brief 订单类型
 */
enum class OrderType { LIMIT, MARKET };

/**
 * @brief 订单数据项
 */
class OrderDataItem : public BaseData {
 public:
  std::string order_id;  ///< 订单ID

  Direction direction;      ///< 交易方向
  dec_float price;          ///< 订单价格
  dec_float volume;         ///< 订单数量
  dec_float filled_volume;  ///< 已成交数量

  OrderType otype;     ///< 订单类型
  OrderStatus status;  ///< 订单状态
};

typedef std::shared_ptr<const OrderDataItem> OrderDataItemPtr;

/**
 * @brief 订单数据集合
 */
class OrderData : public BaseData {
 public:
  std::vector<OrderDataItemPtr> items;  ///< 订单列表
};

typedef std::shared_ptr<const OrderData> OrderDataPtr;

/**
 * @brief 成交数据
 */
class TradeData : public BaseData {
 public:
  std::string trade_id;  ///< 成交ID

  Direction direction;  ///< 交易方向
  dec_float price;      ///< 成交价格
  dec_float volume;     ///< 成交数量
  OrderDataPtr order;   ///< 关联的订单

  const static EventType type = EventType::kTrade;
};

typedef std::shared_ptr<const TradeData> TradeDataPtr;

/**
 * @brief 持仓数据项
 */
class PositionItem {
 public:
  std::string symbol;       ///< 交易对符号
  dec_float volume;         ///< 持仓数量
  Direction direction;      ///< 持仓方向（多头/空头）
  dec_float frozen_volume;  ///< 冻结数量（已下单未成交）
  dec_float price;          ///< 持仓均价
  dec_float pnl;            ///< 持仓盈亏

  const static EventType type = EventType::kPosition;
};

typedef std::shared_ptr<const PositionItem> PositionItemPtr;

/**
 * @brief 持仓数据集合
 */
class PositionData : public BaseData {
 public:
  std::vector<PositionItemPtr> items;  ///< 持仓列表

  const static EventType type = EventType::kPosition;
};

typedef std::shared_ptr<const PositionData> PositionDataPtr;

/**
 * @brief 余额数据项（单个币种）
 */
class BalanceItem {
 public:
  std::string symbol;        ///< 币种符号
  dec_float balance;         ///< 可用余额
  dec_float frozen_balance;  ///< 冻结余额
};

typedef std::shared_ptr<const BalanceItem> BalanceItemPtr;

/**
 * @brief 账户数据
 */
class AccountData : public BaseData {
 public:
  std::string account_id;  ///< 账户ID

  dec_float balance;         ///< 账户总余额
  dec_float frozen_balance;  ///< 总冻结余额

  std::vector<BalanceItemPtr> items;  ///< 各币种余额明细

  const static EventType type = EventType::kAccount;
};

typedef std::shared_ptr<const AccountData> AccountDataPtr;

/// 查询账户请求数据
class QueryAccountData : public BaseData {
 public:
  const static EventType type = EventType::kQueryAccount;
};

typedef std::shared_ptr<const QueryAccountData> QueryAccountDataPtr;

/// 查询持仓请求数据
class QueryPositionData : public BaseData {
 public:
  const static EventType type = EventType::kQueryPosition;
};

typedef std::shared_ptr<const QueryPositionData> QueryPositionDataPtr;

/// 查询订单请求数据
class QueryOrderData : public BaseData {
 public:
  const static EventType type = EventType::kQueryOrder;
};

typedef std::shared_ptr<const QueryOrderData> QueryOrderDataPtr;

/**
 * @brief 通用消息数据
 */
class MessageData : public BaseData {
 public:
  MessageData(const std::string& message) : message(message) {}
  std::string message;  ///< 消息内容

  const static EventType type = EventType::kMessage;
};

typedef std::shared_ptr<const MessageData> MessageDataPtr;

/**
 * @brief 订阅请求数据
 */
class SubscribeData : public BaseData {
 public:
  const static EventType type = EventType::kSubscribeBook;
};

typedef std::shared_ptr<const SubscribeData> SubscribeDataPtr;

}  // namespace engine

#endif  // BITCOINTRADER_MARKET_BASE_OBJECT_H_
