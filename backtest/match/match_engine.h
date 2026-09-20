#ifndef QITRADER_BACKTEST_MATCH_MATCH_ENGINE_H_
#define QITRADER_BACKTEST_MATCH_MATCH_ENGINE_H_

/**
 * @file match_engine.h
 * @brief 模拟撮合引擎
 *
 * 处理回测中的订单撮合逻辑：
 * - 市价单以最新 Tick 价格即时成交
 * - 限价买单当价格 <= 限价时成交
 * - 限价卖单当价格 >= 限价时成交
 */

#include <functional>
#include <string>
#include <vector>

#include "object.h"

namespace backtest::match {

/// 成交回调函数类型
using OnTradeCallback =
    std::function<void(std::shared_ptr<engine::OrderData>, std::shared_ptr<engine::TradeData>)>;

/**
 * @brief 模拟撮合引擎
 *
 * 管理挂单队列，在每次新 Tick 到达时检查是否满足成交条件。
 */
class MatchEngine {
 public:
  MatchEngine() : m_order_counter(0) {}

  /**
   * @brief 设置成交回调
   * @param callback 当订单成交时调用的回调函数
   */
  void setOnTrade(OnTradeCallback callback) { m_on_trade = std::move(callback); }

  /**
   * @brief 提交新订单
   * @param order 订单数据
   * @param current_price 当前最新价格（用于市价单立即成交）
   */
  void submitOrder(std::shared_ptr<engine::OrderData> order, const dec_float& current_price);

  /**
   * @brief 用订单 ID 撤销尚未成交的限价单
   * @param order_id 待撤销订单 ID
   * @return 被撤销的订单，找不到时返回空指针
   */
  std::shared_ptr<engine::OrderData> cancelOrder(const std::string& order_id);

  /**
   * @brief 用新 Tick 数据触发撮合检查
   * @param tick Tick 数据
   */
  void onTick(engine::TickDataPtr tick);

  /**
   * @brief 获取所有成交记录
   * @return const std::vector<std::shared_ptr<engine::TradeData>>& 成交记录
   */
  const std::vector<std::shared_ptr<engine::TradeData>>& trades() const { return m_trades; }

 private:
  /// 尝试撮合一个挂单
  bool tryMatch(std::shared_ptr<engine::OrderData>& order, const dec_float& price,
                int64_t timestamp_ms, const std::string& symbol);

  /// 生成唯一订单/成交 ID
  std::string generateId();

  /// 挂单队列（限价单等待成交）
  std::vector<std::shared_ptr<engine::OrderData>> m_pending_orders;

  /// 所有成交记录
  std::vector<std::shared_ptr<engine::TradeData>> m_trades;

  /// 成交回调
  OnTradeCallback m_on_trade;

  /// ID 计数器
  uint64_t m_order_counter;
};

}  // namespace backtest::match

#endif  // QITRADER_BACKTEST_MATCH_MATCH_ENGINE_H_
