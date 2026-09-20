#ifndef QITRADER_CORE_PORTFOLIO_LEGACY_LEDGER_ADAPTER_H_
#define QITRADER_CORE_PORTFOLIO_LEGACY_LEDGER_ADAPTER_H_

#include <functional>
#include <memory>
#include <string>

#include "core/domain/types.h"
#include "core/portfolio/portfolio_ledger.h"
#include "engine/engine.h"

namespace core::portfolio {

/**
 * @brief 将 Legacy Gateway 的账户和持仓事件单向同步到统一账本。
 *
 * 遵循 design.md Decision 3 的约定：
 * - 账本写入通道只有两条：标准化执行回报，以及本适配器的单向同步；
 * - 策略不再直接写账本，只能读取不可变快照；
 * - 首次账户/持仓事件用于初始化新账本；
 * - 之后持续校准：Legacy 网关是真实成交的账本，执行回报链路并不完整
 *   （成交回报不带手续费，部分回报也可能匹配不到活跃订单而被丢弃），
 *   只做一次性初始化会让统一账本与网关持续漂移，策略据此读到的现金与
 *   持仓是失真的——表现为卖出容量恒为 0、买单超额被拒。
 *   因此差异超过容差时以 Legacy 为准重同步，并按最小间隔限频输出日志。
 */
class LegacyLedgerAdapter {
 public:
  /// 不一致日志回调，参数为可读的差异描述。
  using InconsistencyLogger = std::function<void(const std::string&)>;

  LegacyLedgerAdapter(engine::EnginePtr engine,
                      std::shared_ptr<PortfolioLedger> ledger,
                      InconsistencyLogger logger = {});

  /// 处理 Legacy 账户事件：首次初始化，之后按 Legacy 账本持续校准。
  void onAccount(const engine::AccountData& account);

  /// 处理 Legacy 持仓事件：首次初始化，之后按 Legacy 账本持续校准。
  void onPosition(const engine::PositionData& position);

  /**
   * @brief 在行情与命令都处理完毕后做一次最终一致性校验。
   *
   * 过程中不比较：账户快照是先于成交产生的，与统一账本天然存在时间差，
   * 过程比较只会产生噪音。只有收尾时两侧都稳定，比较才有意义。
   *
   * @return 不一致描述列表，为空表示两侧一致
   */
  std::vector<std::string> verifyFinal() const;

  /// 是否已经完成初始化同步。
  bool initialized() const {
    return m_cash_synced && m_position_synced;
  }

  /// 累计校准次数，用于观测两套账的漂移频率。
  std::size_t reconcileCount() const { return m_reconcile_count; }

 private:
  /// 账本是否仍可被 Legacy 初始状态覆盖。
  bool canAdoptLegacyState() const;

  /// 以 Legacy 账本为准校正现金，返回是否发生了校正。
  bool reconcileCash(const dec_float& cash, const dec_float& frozen, int64_t timestamp_ms);
  /// 以 Legacy 账本为准校正持仓，返回是否发生了校正。
  bool reconcilePositions(const std::vector<domain::PositionSnapshot>& snapshots,
                          int64_t timestamp_ms);
  /// 限频输出校准日志，避免成交密集时刷屏。
  void notify(const std::string& message);

  void collectCashMismatch(const dec_float& legacy_cash,
                           std::vector<std::string>& mismatches) const;
  void collectPositionMismatches(const std::vector<domain::PositionSnapshot>& legacy,
                                 std::vector<std::string>& mismatches) const;

  engine::EnginePtr m_engine;
  std::shared_ptr<PortfolioLedger> m_ledger;
  InconsistencyLogger m_logger;
  bool m_cash_synced{false};
  bool m_position_synced{false};
  dec_float m_latest_cash{0};
  std::vector<domain::PositionSnapshot> m_latest_positions;
  std::size_t m_reconcile_count{0};
  int64_t m_last_notify_ms{0};
  std::size_t m_suppressed_logs{0};
};

}  // namespace core::portfolio

#endif  // QITRADER_CORE_PORTFOLIO_LEGACY_LEDGER_ADAPTER_H_
