#include "paper_gateway.h"

#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <algorithm>
#include <glog/logging.h>

using namespace std::chrono_literals;

namespace market::paper {

PaperGateway::PaperGateway(engine::EnginePtr engine, const dec_float& initial_capital,
                           int report_interval_s, const dec_float& maker_fee_rate,
                           const dec_float& taker_fee_rate)
    : Gateway(engine, "paper"),
      m_initial_capital(initial_capital),
      m_cash(initial_capital),
      m_position_volume(0),
      m_position_avg_price(0),
      m_last_price(0),
      m_maker_fee_rate(maker_fee_rate),
      m_taker_fee_rate(taker_fee_rate),
      m_analyzer(initial_capital),
      m_report_interval_s(report_interval_s > 0 ? report_interval_s : 60) {
  // 设置撮合引擎的成交回调
  m_match_engine.setOnTrade(
      [this](auto order, auto trade) { onTradeEvent(order, trade); });
}

// ============================================================
// 主运行循环：持续监听 OKX 实盘公共 WebSocket
// ============================================================

asio::awaitable<void> PaperGateway::run() {
  LOG(INFO) << "============================================";
  LOG(INFO) << "         模拟交易模式 (Paper Trading)";
  LOG(INFO) << fmt::format("初始资金:     {}", m_initial_capital.str(2, std::ios_base::fixed));
  LOG(INFO) << "行情来源:     OKX 实盘 WebSocket";
  LOG(INFO) << "交易模式:     本地虚拟撮合";
  LOG(INFO) << fmt::format("摘要间隔:     {}s", m_report_interval_s);
  LOG(INFO) << "============================================";

  m_start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();

  // 周期性摘要独立于行情循环，长期运行时便于观察累计效果。
  auto executor = co_await asio::this_coro::executor;
  asio::co_spawn(executor, reportLoop(), asio::detached);
  // 行情看门狗：连接静默挂起时无人报错，必须靠超时检测发现。
  asio::co_spawn(executor, watchdogLoop(), asio::detached);

  co_await watch_public();
  co_return;
}

asio::awaitable<void> PaperGateway::shutdown() {
  LOG(INFO) << "[模拟交易] 正在关闭...";
  m_stopped.store(true);
  // 关闭录制文件，确保已缓冲的行情落盘
  if (m_record) {
    m_record->flush();
    m_record.reset();
  }
  // 必须主动打断 WebSocket：watch_public 此刻多半正挂在 read() 上等待推送，
  // 仅置 m_stopped 不会唤醒它；该协程不退出会一直阻塞引擎停止流程，
  // 表现为 SIGTERM 之后进程无法退出，只能 kill -9。
  if (ws_public_) ws_public_->interrupt();
  // 收尾时输出与回测一致的绩效报告，便于横向比较策略表现。
  LOG(INFO) << fmt::format("[模拟交易] 累计成交 {} 笔，下单请求 {} 次，撤单 {} 次",
                           m_trade_count, m_order_count, m_cancel_count);
  m_analyzer.report(calcEquity());
  co_return;
}

asio::awaitable<void> PaperGateway::reportLoop() {
  auto executor = co_await asio::this_coro::executor;
  while (!m_stopped.load()) {
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::seconds(m_report_interval_s));
    co_await timer.async_wait(asio::use_awaitable);
    if (m_stopped.load()) break;
    reportSnapshot();
  }
}

int64_t PaperGateway::nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

/// 行情停滞超过该秒数即判定连接静默挂起
constexpr int kStallThresholdS = 180;
/// 行情正常时的推送间隔远小于该值，作为看门狗的扫描周期
constexpr int kWatchdogIntervalS = 30;
/// 连续中断该次数后行情仍未恢复，就主动退出进程，交给守护脚本重启。
/// 连接彻底失去自愈能力时（如 read_loop 判死、对端长时间不可达），进程继续
/// 空转毫无意义；而守护脚本只在进程退出时才会把它拉起，不主动退出的话
/// 故障会一直静默下去（线上曾停滞 42 小时无人察觉）。
constexpr int kMaxStallStrikes = 10;

asio::awaitable<void> PaperGateway::watchdogLoop() {
  auto executor = co_await asio::this_coro::executor;
  int64_t last_observed_tick_ms = 0;
  int stall_strikes = 0;
  while (!m_stopped.load()) {
    asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::seconds(kWatchdogIntervalS));
    co_await timer.async_wait(asio::use_awaitable);
    if (m_stopped.load()) break;

    const int64_t last = m_last_tick_time_ms.load();
    if (last <= 0) continue;  // 尚未收到过行情，交给连接阶段处理
    const int64_t stall_s = (nowMs() - last) / 1000;
    if (stall_s < kStallThresholdS) {
      stall_strikes = 0;  // 行情已恢复，重新计数
      continue;
    }

    // 连接可能仍是 ESTABLISHED 却不再推送，read() 会一直挂起，
    // 不主动打断的话策略会静默停摆且无人察觉。
    LOG(ERROR) << fmt::format(
        "[模拟交易] 行情已停滞 {}s，判定连接静默挂起，主动中断以触发重连", stall_s);
    ++m_reconnect_count;
    if (ws_public_) ws_public_->interrupt();

    // 中断后行情时间戳没有推进，说明这次中断同样没能恢复连接。
    if (last == last_observed_tick_ms) {
      ++stall_strikes;
    } else {
      last_observed_tick_ms = last;
      stall_strikes = 1;
    }

    if (stall_strikes >= kMaxStallStrikes) {
      LOG(ERROR) << fmt::format(
          "[模拟交易] 连续 {} 次中断后行情仍未恢复（已停滞 {}s），"
          "连接已失去自愈能力，主动退出进程由守护脚本重启",
          stall_strikes, stall_s);
      // 在独立协程里停止引擎：stop() 会等待各组件退出，
      // 直接在看门狗协程中 await 可能与停止流程互相等待。
      asio::co_spawn(executor, [this]() -> asio::awaitable<void> {
        co_await stop_engine();
      }, asio::detached);
      co_return;
    }
  }
}

void PaperGateway::reportSnapshot() {
  const dec_float equity = calcEquity();
  int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  m_analyzer.addEquitySnapshot(equity, now_ms);

  const dec_float pnl = equity - m_initial_capital;
  const dec_float return_rate = m_initial_capital > 0
      ? pnl / m_initial_capital * dec_float(100) : dec_float(0);
  const auto elapsed_min = m_start_time_ms > 0
      ? (now_ms - m_start_time_ms) / 60000 : 0;

  LOG(INFO) << fmt::format(
      "[模拟交易摘要] 运行 {} 分钟 | 净值 {} | 盈亏 {} ({:.2f}%) | "
      "现金 {} | 冻结 {} | 持仓 {} @ {} | 成交 {} 笔 | 下单 {} 次 | "
      "撤单 {} 次 | 拒单 {} 次 | 手续费 {} | 行情 {} 条 | 重连 {} 次",
      elapsed_min, equity.str(2, std::ios_base::fixed),
      pnl.str(2, std::ios_base::fixed), return_rate.convert_to<double>(),
      m_cash.str(2, std::ios_base::fixed),
      m_reserved_cash.str(2, std::ios_base::fixed),
      m_position_volume.str(6, std::ios_base::fixed),
      m_position_avg_price.str(2, std::ios_base::fixed), m_trade_count,
      m_order_count, m_cancel_count, m_reject_count,
      m_fees.str(2, std::ios_base::fixed), m_tick_count,
      m_reconnect_count);
}

dec_float PaperGateway::calcEquity() const {
  return m_cash + m_position_volume * m_last_price;
}

dec_float PaperGateway::calcFee(const dec_float& turnover, bool is_maker) const {
  if (turnover <= 0) return dec_float(0);
  return turnover * (is_maker ? m_maker_fee_rate : m_taker_fee_rate);
}

void PaperGateway::setRecordPath(const std::string& path) {
  m_record_path = path;
  if (m_record_path.empty()) return;

  m_record = std::make_unique<std::ofstream>(m_record_path, std::ios::out | std::ios::app);
  if (!m_record->is_open()) {
    LOG(ERROR) << fmt::format("[模拟交易] 无法打开录制文件: {}", m_record_path);
    m_record.reset();
    return;
  }

  // 新文件补表头；重放时加载器会跳过首行。
  m_record->seekp(0, std::ios::end);
  if (m_record->tellp() == std::streampos(0)) {
    *m_record << "timestamp_ms,symbol,last_price,volume,open,high,low,close";
    for (std::size_t i = 1; i <= kRecordBookLevels; ++i) {
      *m_record << fmt::format(",bid_px_{},bid_sz_{},ask_px_{},ask_sz_{}", i, i, i, i);
    }
    *m_record << '\n';
  }
  LOG(INFO) << fmt::format("[模拟交易] 开始录制行情到: {}", m_record_path);
}

void PaperGateway::recordTick(const std::shared_ptr<engine::TickData>& tick) {
  if (!m_record || !tick) return;
  const auto& book = tick->order_book;
  // 没有盘口的行情对做市训练没有价值，直接跳过。
  if (!book || book->bids.empty() || book->asks.empty()) return;

  *m_record << tick->timestamp_ms << ',' << tick->symbol << ','
            << tick->last_price.str(8, std::ios_base::fixed) << ','
            << tick->last_volume.str(8, std::ios_base::fixed) << ','
            << tick->open_price.str(8, std::ios_base::fixed) << ','
            << tick->high_price.str(8, std::ios_base::fixed) << ','
            << tick->low_price.str(8, std::ios_base::fixed) << ','
            << tick->last_close_price.str(8, std::ios_base::fixed);

  for (std::size_t i = 0; i < kRecordBookLevels; ++i) {
    const dec_float bid_px = i < book->bids.size() ? book->bids[i].price : dec_float(0);
    const dec_float bid_sz = i < book->bids.size() ? book->bids[i].volume : dec_float(0);
    const dec_float ask_px = i < book->asks.size() ? book->asks[i].price : dec_float(0);
    const dec_float ask_sz = i < book->asks.size() ? book->asks[i].volume : dec_float(0);
    *m_record << ',' << bid_px.str(8, std::ios_base::fixed) << ','
              << bid_sz.str(8, std::ios_base::fixed) << ','
              << ask_px.str(8, std::ios_base::fixed) << ','
              << ask_sz.str(8, std::ios_base::fixed);
  }
  *m_record << '\n';
}

asio::awaitable<void> PaperGateway::watch_public() {
  auto executor = co_await asio::this_coro::executor;
  int retry_count = 0;
  constexpr int max_retry = 20;

  for (;;) {
    if (m_stopped.load()) {
      LOG(INFO) << "[模拟交易] 收到停止信号，退出 WebSocket 循环";
      co_return;
    }

    try {
      // 成功读到一条消息后必须立即继续读取，
      // 否则会误走到下面的退避逻辑，把行情强制限速成每秒一条。
      co_await ws_deal(ws_public_);
      retry_count = 0;
      continue;
    } catch (boost::system::system_error& e) {
      LOG(ERROR) << fmt::format("[模拟交易] WebSocket 错误: {}", e.what());
    } catch (std::runtime_error& e) {
      LOG(ERROR) << fmt::format("[模拟交易] WebSocket 错误: {}", e.what());
    } catch (std::exception& e) {
      LOG(ERROR) << fmt::format("[模拟交易] WebSocket 错误: {}", e.what());
    } catch (...) {
      LOG(ERROR) << "[模拟交易] WebSocket 未知错误";
    }

    if (m_stopped.load()) co_return;

    retry_count++;
    if (retry_count > max_retry) {
      LOG(ERROR) << fmt::format("[模拟交易] 达到最大重试次数 {}，停止重连", max_retry);
      co_return;
    }
    auto delay = std::min(int64_t(1) << (retry_count - 1), int64_t(60));
    LOG(WARNING) << fmt::format("[模拟交易] {}s 后进行第 {} 次重试", delay, retry_count);
    boost::asio::steady_timer timer(executor);
    timer.expires_after(std::chrono::seconds(delay));
    co_await timer.async_wait(asio::use_awaitable);

    // 连接可能已被看门狗中断而失效，重建后需要重新订阅原有品种。
    if (m_stopped.load()) co_return;
    try {
      co_await market_init();
      for (const auto& symbol : m_subscribed_symbols) {
        auto request = std::make_shared<engine::SubscribeData>();
        request->symbol = symbol;
        co_await subscribe_book(request);
        co_await subscribe_tick(request);
      }
      m_last_tick_time_ms.store(nowMs());
      LOG(INFO) << fmt::format("[模拟交易] 已重建连接并重新订阅 {} 个品种",
                               m_subscribed_symbols.size());
    } catch (const std::exception& e) {
      LOG(ERROR) << fmt::format("[模拟交易] 重建行情连接失败: {}", e.what());
    }
  }
}

asio::awaitable<void> PaperGateway::ws_deal(std::shared_ptr<OkxWs> ws) {
  auto msg = co_await ws->read();

  // 处理事件消息（事件消息没有 data 字段，必须提前返回）
  if (!msg.event.empty()) {
    if (msg.event == "error") {
      LOG(ERROR) << fmt::format("[模拟交易] ws error code: {}, message: {}", msg.code, msg.msg);
    } else if (msg.event == "subscribe") {
      LOG(INFO) << fmt::format("[模拟交易] ws subscribe: {}, channel: {}", msg.event, msg.arg.channel);
    } else {
      LOG(INFO) << fmt::format("[模拟交易] ws event: {}", msg.event);
    }
    co_return;
  }

  // 分发行情数据
  if (msg.arg.channel == "books") {
    co_await deal_book(msg.arg.instId, std::any_cast<std::vector<WsBook>>(msg.data));
  } else if (msg.arg.channel == "tickers") {
    co_await deal_tick(msg.arg.instId, std::any_cast<std::vector<WsTick>>(msg.data));
  }
}

// ============================================================
// 行情数据处理（与 OKX 网关逻辑一致）
// ============================================================

asio::awaitable<void> PaperGateway::deal_book(const std::string& symbol,
                                               const std::vector<WsBook>& msg) {
  for (auto& book_item : msg) {
    auto item = std::make_shared<engine::Book>();
    item->symbol = symbol;
    item->exchange = name();
    item->timestamp_ms = book_item.ts;

    for (auto& bid : book_item.bids) {
      auto bid_item = engine::BookItem();
      bid_item.price = bid.price;
      bid_item.volume = bid.size;
      item->bids.push_back(bid_item);
    }

    for (auto& ask : book_item.asks) {
      auto ask_item = engine::BookItem();
      ask_item.price = ask.price;
      ask_item.volume = ask.size;
      item->asks.push_back(ask_item);
    }

    markets_.apply([&item](std::map<std::string, SingleMarket>& map) {
      map[item->symbol].last_book = item;
    });

    co_await on_book(item);
  }
}

asio::awaitable<void> PaperGateway::deal_tick(const std::string& symbol,
                                               const std::vector<WsTick>& msg) {
  ++m_tick_count;
  m_last_tick_time_ms.store(nowMs());
  for (auto& tick_item : msg) {
    auto item = std::make_shared<engine::TickData>();
    item->symbol = symbol;
    item->exchange = name();
    item->timestamp_ms = tick_item.ts;

    item->last_price = tick_item.last;
    item->last_volume = tick_item.lastSz;
    item->turnover = tick_item.lastSz * tick_item.last;

    item->last_close_price = tick_item.open24h;
    item->open_price = tick_item.open24h;
    item->high_price = tick_item.high24h;
    item->low_price = tick_item.low24h;

    markets_.apply([&item](std::map<std::string, SingleMarket>& map) {
      item->order_book = map[item->symbol].last_book;
      map[item->symbol].last_tick = item;
    });

    // 录制必须在 order_book 挂载之后：盘口是做市训练的关键输入。
    recordTick(item);

    // 更新最新价格并触发撮合引擎检查挂单
    m_last_price = item->last_price;
    m_match_engine.onTick(item);

    // 把 onTick 同步撮合成的成交回报给引擎：撮合器已把订单置为 FILLED，
    // 不回报的话上层会一直把它当成活跃挂单，且统一账本不会记账。
    if (!m_pending_fills.empty()) {
      for (auto& filled : m_pending_fills) {
        if (filled) co_await on_order(filled);
      }
      m_pending_fills.clear();
    }

    // 撮合可能产生成交，此时推送一次账户与持仓快照，
    // 让策略和统一账本的 Legacy 同步适配器看到最新状态，
    // 否则收尾时的一致性校验会拿启动时那份陈旧快照做比较。
    if (m_account_dirty.exchange(false)) {
      co_await query_account(nullptr);
      co_await query_position(nullptr);
    }

    co_await on_tick(item);
  }
}

// ============================================================
// 市场初始化：只连接公共 WebSocket（不需要登录私有频道）
// ============================================================

asio::awaitable<void> PaperGateway::market_init() {
  auto ctx = co_await asio::this_coro::executor;
  ws_public_ = std::make_shared<OkxWs>(ctx, 100);

  try {
    co_await ws_public_->connect();
  } catch (const std::exception& e) {
    // 连接失败是最常见的启动问题，这里把排查方向直接给出。
    LOG(ERROR) << fmt::format(
        "[模拟交易] 连接 OKX 公共行情失败: {}。"
        "请确认网络可访问行情端点（默认 wss://ws.okx.com:8443，"
        "配置 sim=true 时为 wss://wspap.okx.com:8443），"
        "必要时检查代理、防火墙或域名解析是否被拦截。",
        e.what());
    throw;
  }
  LOG(INFO) << "[模拟交易] OKX 公共 WebSocket 已连接";
  co_return;
}

// ============================================================
// 虚拟交易操作
// ============================================================

asio::awaitable<void> PaperGateway::send_orders(engine::OrderDataPtr order) {
  if (!order || order->items.empty()) co_return;

  ++m_order_count;
  // 长期运行时下单请求非常密集，只在开启 --v=1 时输出，避免日志膨胀。
  VLOG(1) << fmt::format("[模拟交易] 收到下单请求，共 {} 个子单", order->items.size());
  auto mutable_order = std::const_pointer_cast<engine::OrderData>(order);
  dec_float required_cash(0);
  dec_float required_position(0);
  dec_float reserve_cash(0);
  dec_float reserve_position(0);
  bool valid = true;

  for (const auto& item : mutable_order->items) {
    if (!item || item->status != engine::OrderStatus::SUBMITTING || item->volume <= 0) {
      valid = false;
      break;
    }
    const dec_float execution_price = item->otype == engine::OrderType::MARKET
        ? m_last_price : item->price;
    const std::string symbol = item->symbol.empty() ? mutable_order->symbol : item->symbol;
    if (execution_price <= 0 || symbol.empty()) {
      valid = false;
      break;
    }
    if (item->direction == engine::Direction::BUY) {
      required_cash += execution_price * item->volume;
      if (item->otype == engine::OrderType::LIMIT) {
        reserve_cash += item->price * item->volume;
      }
    } else {
      if (!m_position_symbol.empty() && symbol != m_position_symbol) {
        valid = false;
        break;
      }
      required_position += item->volume;
      if (item->otype == engine::OrderType::LIMIT) reserve_position += item->volume;
    }
  }

  if (required_cash > m_cash - m_reserved_cash ||
      required_position > m_position_volume - m_reserved_position_volume) {
    valid = false;
  }
  if (!valid) {
    // 拒绝通常意味着冻结额度不足，计数后进摘要，便于快速定位策略哑火。
    ++m_reject_count;
    for (const auto& item : mutable_order->items) {
      if (!item) continue;
      auto mutable_item = std::const_pointer_cast<engine::OrderDataItem>(item);
      mutable_item->status = engine::OrderStatus::REJECTED;
      mutable_item->filled_volume = dec_float(0);
    }
    co_await on_order(order);
    co_return;
  }

  m_reserved_cash += reserve_cash;
  m_reserved_position_volume += reserve_position;
  m_match_engine.submitOrder(mutable_order, m_last_price);
  // submitOrder 内部若立即撮合成交，同样会走 onTradeEvent 记入待回报列表；
  // 紧接着下面就会用 on_order 回报整笔订单，先清空避免同一笔成交回报两次。
  m_pending_fills.clear();
  co_await on_order(order);
}

asio::awaitable<void> PaperGateway::cancel_order(engine::OrderDataPtr order) {
  if (!order) co_return;

  for (const auto& item : order->items) {
    if (!item || item->order_id.empty()) continue;
    auto cancelled = m_match_engine.cancelOrder(item->order_id);
    if (!cancelled) continue;

    // 必须释放该挂单占用的冻结资金或持仓：
    // 否则冻结额度会随每次撤单不断累积，几十次撤单后可用额度就被耗尽，
    // 之后所有新单都会因"可用资金/持仓不足"被拒，表现为策略长期哑火。
    for (const auto& cancelled_item : cancelled->items) {
      if (!cancelled_item || cancelled_item->order_id != item->order_id) continue;
      const dec_float remaining = cancelled_item->volume - cancelled_item->filled_volume;
      if (cancelled_item->direction == engine::Direction::BUY &&
          cancelled_item->otype == engine::OrderType::LIMIT) {
        m_reserved_cash -= cancelled_item->price * remaining;
        if (m_reserved_cash < 0) m_reserved_cash = dec_float(0);
      } else if (cancelled_item->direction == engine::Direction::SELL &&
                 cancelled_item->otype == engine::OrderType::LIMIT) {
        m_reserved_position_volume -= remaining;
        if (m_reserved_position_volume < 0) m_reserved_position_volume = dec_float(0);
      }
    }

    ++m_cancel_count;
    VLOG(1) << fmt::format("[模拟交易] 撤销挂单: {}", item->order_id);
    co_await on_order(cancelled);
  }
}

asio::awaitable<void> PaperGateway::query_account(engine::QueryAccountDataPtr data) {
  auto account = std::make_shared<engine::AccountData>();
  account->account_id = "paper";
  account->exchange = "paper";
  account->balance = m_cash;
  account->frozen_balance = m_reserved_cash;

  auto balance_item = std::make_shared<engine::BalanceItem>();
  balance_item->symbol = "USDT";
  balance_item->balance = m_cash - m_reserved_cash;
  balance_item->frozen_balance = m_reserved_cash;
  account->items.push_back(balance_item);

  // 成交后会频繁推送账户快照，明细日志只在 --v=1 时输出
  VLOG(1) << fmt::format("[模拟交易] 查询账户: 余额 {} USDT",
                         m_cash.str(2, std::ios_base::fixed));

  co_await on_account(account);
}

asio::awaitable<void> PaperGateway::query_position(engine::QueryPositionDataPtr data) {
  auto position = std::make_shared<engine::PositionData>();
  position->exchange = "paper";

  if (m_position_volume > 0) {
    auto item = std::make_shared<engine::PositionItem>();
    item->symbol = m_position_symbol;
    item->volume = m_position_volume;
    item->direction = engine::Direction::BUY;
    item->frozen_volume = dec_float(0);
    item->price = m_position_avg_price;
    item->pnl = (m_last_price - m_position_avg_price) * m_position_volume;
    position->items.push_back(item);

    VLOG(1) << fmt::format("[模拟交易] 查询持仓: {} {} @ {}，浮动盈亏 {}",
                           m_position_symbol, m_position_volume.str(6, std::ios_base::fixed),
                           m_position_avg_price.str(2, std::ios_base::fixed),
                           item->pnl.str(2, std::ios_base::fixed));
  } else {
    VLOG(1) << "[模拟交易] 查询持仓: 无持仓";
  }

  co_await on_position(position);
}

asio::awaitable<void> PaperGateway::query_order(engine::QueryOrderDataPtr data) {
  // 模拟交易暂不支持历史订单查询
  co_return;
}

asio::awaitable<void> PaperGateway::subscribe_book(engine::SubscribeDataPtr data) {
  if (!data) co_return;

  m_subscribed_symbols.insert(data->symbol);

  auto sub_req = WsSubscibeRequest();
  sub_req.op = "subscribe";
  sub_req.args = {{"books", data->symbol}};
  co_await ws_public_->write(sub_req);

  LOG(INFO) << "[模拟交易] 订阅订单簿: " << data->symbol;
  co_return;
}

asio::awaitable<void> PaperGateway::subscribe_tick(engine::SubscribeDataPtr data) {
  if (!data) co_return;

  m_subscribed_symbols.insert(data->symbol);

  auto sub_req = WsSubscibeRequest();
  sub_req.op = "subscribe";
  sub_req.args = {{"tickers", data->symbol}};
  co_await ws_public_->write(sub_req);

  LOG(INFO) << "[模拟交易] 订阅 Tick: " << data->symbol;
  co_return;
}

void PaperGateway::unsubscribe(const std::string& symbol) {
  m_subscribed_symbols.erase(symbol);
}

// ============================================================
// 虚拟成交事件处理：更新账户余额和持仓
// ============================================================

void PaperGateway::onTradeEvent(std::shared_ptr<engine::OrderData> order,
                                 std::shared_ptr<engine::TradeData> trade) {
  if (!trade) return;

  // 标记账户已变化，下一次行情处理时推送最新快照。
  m_account_dirty.store(true);
  ++m_trade_count;
  // 与回测共用同一套绩效统计，长期运行也能得到相同的指标口径。
  m_analyzer.addTrade(trade);

  // 本函数是撮合器的同步回调，在这里无法推送事件。先把订单攒起来，
  // 交给行情处理协程回报成交——不回报的话上层订单管理器永远不知道这笔
  // 订单已结束，会一直把它当作活跃挂单（active_orders 无限堆积），
  // 统一账本也收不到执行回报，策略读到的持仓与现金将始终是失真值。
  if (order) m_pending_fills.push_back(order);

  bool is_limit_order = false;
  if (trade->order) {
    for (const auto& item : trade->order->items) {
      if (item && item->direction == trade->direction &&
          item->otype == engine::OrderType::LIMIT) {
        is_limit_order = true;
        break;
      }
    }
  }
  if (is_limit_order) {
    if (trade->direction == engine::Direction::BUY) {
      const dec_float amount = trade->price * trade->volume;
      m_reserved_cash = m_reserved_cash > amount ? m_reserved_cash - amount : dec_float(0);
    } else {
      m_reserved_position_volume = m_reserved_position_volume > trade->volume
          ? m_reserved_position_volume - trade->volume : dec_float(0);
    }
  }

  if (trade->direction == engine::Direction::BUY) {
    dec_float cost = trade->price * trade->volume;
    // 手续费必须计入：做市策略成交流水极大，零成本会让绩效被系统性高估，
    // 甚至把实盘必亏的策略显示成盈利。限价单是挂单被吃（maker），
    // 市价单是主动吃单（taker），两者费率不同。
    const dec_float fee = calcFee(cost, is_limit_order);
    m_fees += fee;
    m_cash -= cost + fee;

    // 更新持仓均价（含买入手续费，否则卖出时的已实现盈亏会偏高）
    dec_float total_cost = m_position_avg_price * m_position_volume + cost + fee;
    m_position_volume += trade->volume;
    if (m_position_volume > 0) {
      m_position_avg_price = total_cost / m_position_volume;
    }
    m_position_symbol = trade->symbol;

    // 成交非常密集，明细只在 --v=1 时输出；累计笔数见周期性摘要。
    VLOG(1) << fmt::format("[模拟交易] 买入成交: {} {} @ {}，剩余资金 {}",
                           trade->symbol, trade->volume.str(6, std::ios_base::fixed),
                           trade->price.str(2, std::ios_base::fixed),
                           m_cash.str(2, std::ios_base::fixed));
  } else {
    dec_float revenue = trade->price * trade->volume;
    const dec_float fee = calcFee(revenue, is_limit_order);
    m_fees += fee;
    m_cash += revenue - fee;
    m_position_volume -= trade->volume;

    // 持仓均价已含买入手续费，这里再扣卖出手续费，得到双边成本后的真实盈亏
    dec_float pnl = (trade->price - m_position_avg_price) * trade->volume - fee;

    if (m_position_volume <= 0) {
      m_position_volume = dec_float(0);
      m_position_avg_price = dec_float(0);
    }

    VLOG(1) << fmt::format("[模拟交易] 卖出成交: {} {} @ {}，盈亏 {}，剩余资金 {}",
                           trade->symbol, trade->volume.str(6, std::ios_base::fixed),
                           trade->price.str(2, std::ios_base::fixed),
                           pnl.str(2, std::ios_base::fixed),
                           m_cash.str(2, std::ios_base::fixed));
  }
}

}  // namespace market::paper
