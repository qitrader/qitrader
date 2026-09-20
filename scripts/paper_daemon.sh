#!/usr/bin/env bash
#
# 模拟交易守护进程（运行在目标机器上）
#
# 以后台方式持续拉起交易进程，并在意外退出后自动重启。
# 拆成独立文件是为了避免 `nohup bash -c '...'` 里的嵌套引号展开问题——
# 变量直接通过参数传入，脚本内部保持普通写法。
#
# 用法：
#   paper_daemon.sh <BIN> <ARGS_FILE> <LOG_FILE>
#
set -uo pipefail

BIN="${1:?缺少二进制路径}"
ARGS_FILE="${2:?缺少参数文件}"
LOG_FILE="${3:?缺少日志文件}"

MAX_LOG_BYTES=209715200    # 200MB：单文件上限，超过则切割
SPIKE_THRESHOLD=524288000  # 500MB：单次运行的日志增长上限，超过视为错误风暴

mkdir -p "$(dirname "$LOG_FILE")"

file_size() {
  if [ -f "$LOG_FILE" ]; then stat -c%s "$LOG_FILE"; else echo 0; fi
}

restart=0
while true; do
  # 日志轮转
  if [ "$(file_size)" -gt "$MAX_LOG_BYTES" ]; then
    mv "$LOG_FILE" "$LOG_FILE.$(date +%Y%m%d_%H%M%S)"
  fi

  size_before=$(file_size)
  # shellcheck disable=SC2046  # 参数文件为每行一个参数，需要按空格展开
  "$BIN" $(cat "$ARGS_FILE") >> "$LOG_FILE" 2>&1
  rc=$?
  size_after=$(file_size)
  growth=$(( size_after - size_before ))

  # 兜底：单次运行产生异常体量的日志说明可能陷入错误风暴，
  # 此时反复重启只会更快耗尽磁盘，改为停止并等待人工介入。
  if [ "$growth" -gt "$SPIKE_THRESHOLD" ]; then
    echo "[守护] $(date '+%F %T') 日志异常增长 $((growth / 1048576))MB，疑似错误风暴，停止自动重启，请检查日志后手动启动" >> "$LOG_FILE"
    exit 1
  fi

  restart=$((restart + 1))
  echo "[守护] $(date '+%F %T') 进程退出 rc=${rc}（本次日志 +$((growth / 1024))KB），5 秒后第 ${restart} 次重启" >> "$LOG_FILE"
  sleep 5
done
