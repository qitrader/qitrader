#!/usr/bin/env bash
#
# 通用策略运行时 smoke test
#
# 验证每个策略在不同执行端口下都能：
#   1. 完整跑完回测并输出绩效报告；
#   2. 以 0 退出码结束，不出现 ERROR / 崩溃 / 堆损坏；
#   3. 行情数据源已接通，运行时上下文已注入。
#
# 用法：
#   scripts/runtime_smoke_test.sh              # 直接跑已构建的二进制
#   scripts/runtime_smoke_test.sh --build      # 先构建再跑
#   BIN=build/.../qitrader scripts/runtime_smoke_test.sh
#
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

BUILD=0
for arg in "$@"; do
  case "$arg" in
    --build) BUILD=1 ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "未知参数: $arg"; exit 1 ;;
  esac
done

if [[ $BUILD -eq 1 ]]; then
  echo "==> 构建 qitrader"
  xmake build -y qitrader || exit 1
fi

BIN="${BIN:-build/linux/x86_64/debug/qitrader}"
if [[ ! -x "$BIN" ]]; then
  echo "找不到可执行文件: $BIN（可先运行 scripts/runtime_smoke_test.sh --build）"
  exit 1
fi

DATA="${DATA:-data/sample_btc_usdt.csv}"
SYMBOL="${SYMBOL:-BTC-USDT}"
if [[ ! -f "$DATA" ]]; then
  echo "找不到回测数据文件: $DATA"
  exit 1
fi

STRATEGIES=(testing grid multilevel)
# 执行端口：auto 按运行模式推断，回测下解析为 backtest
VENUES=(auto backtest)
TIMEOUT="${TIMEOUT:-30}"

passed=0
failed=0

run_case() {
  local strategy="$1" venue="$2"

  local log
  log="$(mktemp)"
  trap 'rm -f "$log"' RETURN

  timeout "$TIMEOUT" "$BIN" --backtest --venue "$venue" \
    --strategy "$strategy" --symbol "$SYMBOL" --data-file "$DATA" \
    --grid-upper 97500 --grid-lower 96800 --grid-count 5 --grid-amount 0.01 \
    --mm-levels 2 --mm-order-size 0.01 --mm-decision-interval-ms 0 \
    > "$log" 2>&1
  local rc=$?

  local problems=()
  [[ $rc -ne 0 ]] && problems+=("退出码=$rc")
  grep -q "回测完成" "$log" || problems+=("未输出绩效报告")
  grep -qE "ERROR|terminate|corrupted|Aborted" "$log" && problems+=("日志含错误或崩溃")
  grep -q "未收到行情快照" "$log" && problems+=("行情数据源未接通")
  grep -q "未注入策略运行时上下文" "$log" && problems+=("运行时上下文未注入")

  if [[ ${#problems[@]} -eq 0 ]]; then
    printf "  [PASS] %-11s venue=%-9s\n" "$strategy" "$venue"
    passed=$((passed + 1))
  else
    printf "  [FAIL] %-11s venue=%-9s %s\n" "$strategy" "$venue" "${problems[*]}"
    cp "$log" "/tmp/smoke_${strategy}_${venue}.log"
    echo "         日志: /tmp/smoke_${strategy}_${venue}.log"
    failed=$((failed + 1))
  fi
}

echo "==> 回测 smoke test（数据: $DATA）"
for strategy in "${STRATEGIES[@]}"; do
  for venue in "${VENUES[@]}"; do
    run_case "$strategy" "$venue"
  done
done

echo "==> 核心运行时单元测试"
TEST_BIN="${TEST_BIN:-build/linux/x86_64/debug/qitrader-core-tests}"
if [[ -x "$TEST_BIN" ]]; then
  if "$TEST_BIN" > /tmp/smoke_core_tests.log 2>&1; then
    echo "  [PASS] 核心运行时单元测试"
    passed=$((passed + 1))
  else
    echo "  [FAIL] 核心运行时单元测试（详见 /tmp/smoke_core_tests.log）"
    failed=$((failed + 1))
  fi
else
  echo "  [SKIP] 未找到单元测试: $TEST_BIN"
fi

echo
echo "结果: $passed 通过, $failed 失败"
[[ $failed -eq 0 ]] || exit 1
