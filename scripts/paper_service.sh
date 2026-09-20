#!/usr/bin/env bash
#
# 模拟交易长期运行管理脚本
#
# 以后台方式长期运行 Paper Trading，并在进程意外退出时自动重启，
# 同时提供状态查看与实时日志跟踪，便于随时观察策略效果。
#
# 用法：
#   ./paper_service.sh start            # 后台启动（带自动重启）
#   ./paper_service.sh stop             # 优雅停止
#   ./paper_service.sh restart          # 重启
#   ./paper_service.sh status           # 查看运行状态与最近摘要
#   ./paper_service.sh tail             # 实时跟踪日志
#   ./paper_service.sh report           # 查看绩效报告（需先停止）
#
# 可通过环境变量覆盖默认参数：
#   SYMBOL=ETH-USDT CAPITAL=1000 STRATEGY=multilevel INTERVAL=60 ./paper_service.sh start
#
set -uo pipefail

BASE_DIR="${BASE_DIR:-/root/qitrader}"
BIN="$BASE_DIR/qitrader"
LOG_DIR="$BASE_DIR/logs"
LOG_FILE="$LOG_DIR/paper.log"
PID_FILE="$BASE_DIR/paper.pid"

SYMBOL="${SYMBOL:-ETH-USDT}"
CAPITAL="${CAPITAL:-1000}"
STRATEGY="${STRATEGY:-multilevel}"
INTERVAL="${INTERVAL:-60}"
CONFIG="${CONFIG:-$BASE_DIR/config.ini}"

# 策略相关参数：按需调整或通过环境变量覆盖
# 模型落盘路径：非空时启动时加载、关闭时保存，使在线学习成果可跨次累积。
MODEL_PATH="${MODEL_PATH:-}"

# 行情录制路径：非空时边训练边把带盘口的行情存成 CSV，供离线重放训练。
RECORD_FILE="${RECORD_FILE:-}"

# 手续费率：默认 OKX 现货 Lv1（maker 0.08% / taker 0.1%）。
# 做市策略几乎全是 maker 成交，费率直接决定盈亏，请按自己的 VIP 等级调整。
MAKER_FEE="${MAKER_FEE:-0.0008}"
TAKER_FEE="${TAKER_FEE:-0.001}"

MM_LEVELS="${MM_LEVELS:-3}"
MM_SIZE="${MM_SIZE:-0.01}"
MM_INTERVAL_MS="${MM_INTERVAL_MS:-5000}"
# 每次决策的挂单 lot 数与库存上限。程序默认值（budget=20 / limit=20）对 1000 USDT
# 的本金严重偏大：库存特征恒为 0、买单超限被拒，因此这里按本金规模给出默认值。
MM_BUDGET="${MM_BUDGET:-5}"
MM_INVENTORY_LIMIT="${MM_INVENTORY_LIMIT:-0.2}"
# 单边报价最小偏移（bps）。必须设到费率量级：贴着盘口报价时往返价差（ETH 约 3 bps）
# 覆盖不了双边手续费（16 bps），每笔必亏。实测 24 bps 可把回测亏损从 -24% 收敛到 -0.2%。
MM_MIN_HALF_SPREAD_BPS="${MM_MIN_HALF_SPREAD_BPS:-24}"
# 库存偏斜强度（bps）：持多头时保留价下移，把库存拉回中性，压缩期末持仓的浮亏。
MM_SKEW_BPS="${MM_SKEW_BPS:-15}"
GRID_LOWER="${GRID_LOWER:-}"
GRID_UPPER="${GRID_UPPER:-}"
GRID_COUNT="${GRID_COUNT:-10}"
GRID_AMOUNT="${GRID_AMOUNT:-0.01}"

build_args() {
  local args=(--paper --strategy "$STRATEGY" --symbol "$SYMBOL"
              --initial-capital "$CAPITAL" --report-interval "$INTERVAL"
              --maker-fee-rate "$MAKER_FEE" --taker-fee-rate "$TAKER_FEE"
              -c "$CONFIG")
  if [[ -n "$MODEL_PATH" ]]; then
    args+=(--model-path "$MODEL_PATH")
  fi
  if [[ -n "$RECORD_FILE" ]]; then
    args+=(--record-file "$RECORD_FILE")
  fi
  if [[ "$STRATEGY" == "multilevel" ]]; then
    args+=(--mm-levels "$MM_LEVELS" --mm-order-size "$MM_SIZE"
           --mm-decision-interval-ms "$MM_INTERVAL_MS")
    if [[ -n "$MM_BUDGET" ]]; then
      args+=(--mm-order-budget "$MM_BUDGET")
    fi
    if [[ -n "$MM_INVENTORY_LIMIT" ]]; then
      args+=(--mm-inventory-limit "$MM_INVENTORY_LIMIT")
    fi
    if [[ -n "$MM_MIN_HALF_SPREAD_BPS" ]]; then
      args+=(--mm-min-half-spread-bps "$MM_MIN_HALF_SPREAD_BPS")
    fi
    if [[ -n "$MM_SKEW_BPS" ]]; then
      args+=(--mm-inventory-skew-bps "$MM_SKEW_BPS")
    fi
  elif [[ "$STRATEGY" == "grid" ]]; then
    if [[ -z "$GRID_LOWER" || -z "$GRID_UPPER" ]]; then
      echo "网格策略需要设置 GRID_LOWER 和 GRID_UPPER"
      return 1
    fi
    args+=(--grid-lower "$GRID_LOWER" --grid-upper "$GRID_UPPER"
           --grid-count "$GRID_COUNT" --grid-amount "$GRID_AMOUNT")
  fi
  printf '%s\n' "${args[@]}"
}

is_running() {
  [[ -f "$PID_FILE" ]] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null
}

do_start() {
  if is_running; then
    echo "已在运行，PID=$(cat "$PID_FILE")。如需重启请先执行 stop。"
    return 0
  fi
  if [[ ! -x "$BIN" ]]; then
    echo "找不到可执行文件: $BIN"
    return 1
  fi
  mkdir -p "$LOG_DIR"

  # 守护逻辑放在独立脚本里：nohup bash -c '...' 内嵌复杂引号容易展开出错。
  local daemon="$BASE_DIR/paper_daemon.sh"
  if [[ ! -f "$daemon" ]]; then
    echo "缺少守护脚本: $daemon（请与 paper_service.sh 一同上传）"
    return 1
  fi
  chmod +x "$daemon"
  nohup "$daemon" "$BIN" "$BASE_DIR/.paper_args" "$LOG_FILE" >/dev/null 2>&1 &

  local pid=$!
  echo "$pid" > "$PID_FILE"
  sleep 2
  echo "已启动，守护进程 PID=$pid"
  echo "日志: $LOG_FILE"
}

do_stop() {
  if [[ -f "$PID_FILE" ]]; then
    local pid
    pid=$(cat "$PID_FILE")
    # 先停守护进程，避免停止后又被拉起
    kill "$pid" 2>/dev/null && echo "已停止守护进程 $pid"
    rm -f "$PID_FILE"
  fi
  # 给交易进程发 SIGTERM，触发优雅关闭并输出绩效报告。
  # 用二进制路径锚定匹配，避免误杀执行本脚本的 shell（其命令行含相同字符串）。
  if pkill -TERM -f "^$BIN" 2>/dev/null; then
    echo "已向交易进程发送 SIGTERM，等待优雅退出..."
    sleep 3
  else
    echo "未发现运行中的交易进程"
  fi
}

do_status() {
  echo "=========== 运行状态 ==========="
  if is_running; then
    echo "状态: 运行中（守护进程 PID=$(cat "$PID_FILE")）"
    pgrep -af "^$BIN" | head -1
  else
    echo "状态: 未运行"
  fi

  if [[ ! -f "$LOG_FILE" ]]; then
    echo "尚无日志文件: $LOG_FILE"
    return 0
  fi

  echo
  echo "=========== 最近摘要 ==========="
  grep '\[模拟交易摘要\]' "$LOG_FILE" | tail -5

  echo
  echo "=========== 统计 ==========="
  local started
  started=$(grep -c '模拟交易模式 (Paper Trading)' "$LOG_FILE")
  # 成交明细默认不落盘（VLOG），累计值从最近一条摘要里取
  local last_summary trades orders cancels rejects ticks reconnects
  last_summary=$(grep '\[模拟交易摘要\]' "$LOG_FILE" | tail -1)
  trades=$(echo "$last_summary" | grep -oE '成交 [0-9]+ 笔' | grep -oE '[0-9]+')
  orders=$(echo "$last_summary" | grep -oE '下单 [0-9]+ 次' | grep -oE '[0-9]+')
  cancels=$(echo "$last_summary" | grep -oE '撤单 [0-9]+ 次' | grep -oE '[0-9]+')
  rejects=$(echo "$last_summary" | grep -oE '拒单 [0-9]+ 次' | grep -oE '[0-9]+')
  ticks=$(echo "$last_summary" | grep -oE '行情 [0-9]+ 条' | grep -oE '[0-9]+')
  reconnects=$(echo "$last_summary" | grep -oE '重连 [0-9]+ 次' | grep -oE '[0-9]+')

  echo "启动次数:   $started"
  echo "自动重启:   $(grep -c '\[守护\].*次重启' "$LOG_FILE")"
  echo "成交笔数:   ${trades:-0}"
  echo "下单请求:   ${orders:-0}"
  echo "撤单次数:   ${cancels:-0}"
  echo "拒单次数:   ${rejects:-0}   （持续偏高通常意味着冻结额度或持仓不足）"
  echo "行情条数:   ${ticks:-0}"
  echo "重连次数:   ${reconnects:-0}"
  # glog 行首是等级字符（E=ERROR / W=WARNING / I=INFO），
  # 用 ' ERROR ' 匹配会永远为 0，从而掩盖真实故障。
  echo "ERROR 数:   $(grep -cE '^E[0-9]{8}' "$LOG_FILE")"
  echo "崩溃迹象:   $(grep -cE 'corrupted|dumped core|Segmentation' "$LOG_FILE")"
  echo
  echo "日志大小:   $(du -h "$LOG_FILE" | awk '{print $1}')"
  echo "最后更新:   $(stat -c '%y' "$LOG_FILE" 2>/dev/null | cut -d'.' -f1)"
}

do_tail() {
  [[ -f "$LOG_FILE" ]] || { echo "尚无日志文件"; return 1; }
  tail -f "$LOG_FILE"
}

do_report() {
  [[ -f "$LOG_FILE" ]] || { echo "尚无日志文件"; return 1; }
  echo "=========== 绩效报告 ==========="
  awk '/回测绩效报告/,/^===.*$/' "$LOG_FILE" | tail -20
  echo
  echo "=========== 收盘汇总 ==========="
  grep -E '累计成交|最终账本' "$LOG_FILE" | tail -4
}

case "${1:-}" in
  start)
    build_args > "$BASE_DIR/.paper_args" || exit 1
    do_start
    ;;
  stop)    do_stop ;;
  restart) do_stop; sleep 1; build_args > "$BASE_DIR/.paper_args" || exit 1; do_start ;;
  status)  do_status ;;
  tail)    do_tail ;;
  report)  do_report ;;
  *)
    sed -n '2,20p' "$0"
    exit 1
    ;;
esac
