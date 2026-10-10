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

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "object.h"

namespace backtest::match {

/// 成交回调函数类型
using OnTradeCallback =
    std::function<void(std::shared_ptr<engine::OrderData>, std::shared_ptr<engine::TradeData>)>;

/**
 * @brief 成交保真度模型配置
 *
 * 朴素的"价格穿越即全部成交"会系统性高估成交质量：真实市场里挂单要排队，
 * 轮到你时往往已经发生了逆向选择（价格正朝不利方向移动）。这里用"前置队列
 * 被主动成交量吃掉才成交"来近似排队，用滑点近似逆向选择成本。
 */
struct FillModelConfig {
  /// 启用队列模型；关闭则退回"价格穿越即成交"的旧行为
  bool queue_model{true};
  /// 每笔主动成交量里能用于吃掉前置队列的比例
  dec_float fill_ratio{dec_float("0.3")};
  /// 前置排队量的上界 = 挂单量 × 该系数。
  /// 盘口累计量通常是挂单量的几十上百倍，直接用它会让小单永远排不到
  /// （线上实测 16 小时零成交）。取挂单量的若干倍作为上界：既保留"要排队"
  /// 的约束，又不会把小额做市单判成永不成成交。
  dec_float queue_depth_factor{dec_float("3")};
  /// 逆向选择滑点（bps）：0 关闭，>0 时成交价朝不利方向偏移
  int adverse_slippage_bps{0};
  /// 行情没有成交量时的退化成交量（K 线回放场景）
  dec_float default_trade_volume{dec_float("1")};
};

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

  /// 设置成交保真度模型（队列排队 + 逆向选择滑点）
  void setFillModel(const FillModelConfig& config) { m_fill_model = config; }
  const FillModelConfig& fillModel() const { return m_fill_model; }

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

  /**
   * @brief 回收已被消费的成交记录，避免长周期回放下成交列表无限增长。
   * @param cursor 调用方已处理到的下标
   */
  void trimTradesBefore(std::size_t cursor);

 private:
  /// 尝试撮合一个挂单（逐个子单处理，全部成交才算撮合完成）
  bool tryMatch(std::shared_ptr<engine::OrderData>& order, const dec_float& price,
                int64_t timestamp_ms, const std::string& symbol,
                const engine::BookPtr& book, const dec_float& aggressive_volume);

  /// 撮合单个子单，成交时生成成交记录
  bool tryMatchItem(std::shared_ptr<engine::OrderData>& order,
                    const std::shared_ptr<const engine::OrderDataItem>& item,
                    const dec_float& price, int64_t timestamp_ms,
                    const std::string& symbol, const engine::BookPtr& book,
                    const dec_float& aggressive_volume);

  /// 生成唯一订单/成交 ID
  std::string generateId();

  /// 挂单时估算前置排队量：同侧、价格不劣于该限价的档位累计量
  dec_float computeQueueAhead(const engine::BookPtr& book, engine::Direction direction,
                              const dec_float& limit_price, const dec_float& volume) const;

  /// 本 Tick 可动用的成交量：主动成交量 × fill_ratio（无成交量时用退化值）
  dec_float aggressiveVolume(const dec_float& tick_volume) const;

  /// 逆向选择滑点：成交价朝不利方向偏移
  dec_float applyAdverseSlippage(engine::Direction direction, const dec_float& price) const;

  /// 挂单队列（限价单等待成交）
  std::vector<std::shared_ptr<engine::OrderData>> m_pending_orders;

  /// 所有成交记录
  std::vector<std::shared_ptr<engine::TradeData>> m_trades;

  /// 成交回调
  OnTradeCallback m_on_trade;

  /// ID 计数器
  uint64_t m_order_counter;

  /// 成交保真度模型配置
  FillModelConfig m_fill_model;

  /// 子单 ID -> 剩余前置排队量（成交或撤单后清理，避免长期运行泄漏）
  std::unordered_map<std::string, dec_float> m_queue_ahead;

  /// symbol -> 已推进过队列的 Tick 时间戳。回测一帧会调两次 onTick
  /// （sink 里的 venue 一次、网关自己一次），不去重会把队列推进速度翻倍。
  std::unordered_map<std::string, int64_t> m_last_applied_tick_ms;

  /// 最近一次见到的盘口。调用方提交订单时不带盘口，用它做兜底。
  engine::BookPtr m_last_book;
};

}  // namespace backtest::match

#endif  // QITRADER_BACKTEST_MATCH_MATCH_ENGINE_H_
