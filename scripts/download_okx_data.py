#!/usr/bin/env python3
"""
OKX 历史数据下载脚本

从 OKX V5 公开 API 下载历史 K线数据，转换为回测系统所需的 Tick CSV 格式。
无需 API Key，使用公开市场数据接口。

使用方式:
    python3 scripts/download_okx_data.py --symbol BTC-USDT --bar 1m --days 30
    python3 scripts/download_okx_data.py --symbol ETH-USDT --bar 1H --start 2025-01-01 --end 2025-06-30
    python3 scripts/download_okx_data.py --symbol BTC-USDT --bar 1m --days 7 --output data/btc_tick.csv

支持的 K线周期 (bar):
    1m, 3m, 5m, 15m, 30m  — 分钟线
    1H, 2H, 4H, 6H, 12H   — 小时线
    1D, 1W, 1M              — 日/周/月线
"""

import argparse
import csv
import json
import os
import sys
import time
from datetime import datetime, timedelta, timezone
from urllib.request import urlopen, Request
from urllib.error import HTTPError, URLError


# OKX V5 公开 API 基础地址
BASE_URL = "https://www.okx.com"

# 历史 K线接口（支持获取更早的数据）
HISTORY_CANDLES_PATH = "/api/v5/market/history-candles"

# 最近 K线接口（获取最新数据，更快）
CANDLES_PATH = "/api/v5/market/candles"

# 每次请求最大返回条数
MAX_LIMIT = 100

# 请求间隔（秒），避免触发限流
REQUEST_INTERVAL = 0.15


def ts_to_ms(dt: datetime) -> int:
    """将 datetime 转换为毫秒时间戳"""
    return int(dt.timestamp() * 1000)


def ms_to_dt(ms: int) -> datetime:
    """将毫秒时间戳转换为 datetime (UTC)"""
    return datetime.fromtimestamp(ms / 1000, tz=timezone.utc)


def ms_to_str(ms: int) -> str:
    """将毫秒时间戳转换为可读字符串"""
    return ms_to_dt(ms).strftime("%Y-%m-%d %H:%M:%S")


def fetch_json(url: str, max_retries: int = 3) -> dict:
    """
    请求 OKX API 并返回 JSON 响应

    Args:
        url: 完整的 API URL
        max_retries: 最大重试次数

    Returns:
        解析后的 JSON 字典
    """
    for attempt in range(max_retries):
        try:
            req = Request(url, headers={"User-Agent": "qitrader-downloader/1.0"})
            with urlopen(req, timeout=30) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                if data.get("code") != "0":
                    msg = data.get("msg", "未知错误")
                    print(f"  API 错误: code={data.get('code')}, msg={msg}")
                    if attempt < max_retries - 1:
                        time.sleep(1)
                        continue
                    return data
                return data
        except (HTTPError, URLError, TimeoutError) as e:
            print(f"  请求失败 (第 {attempt + 1} 次): {e}")
            if attempt < max_retries - 1:
                time.sleep(2 ** attempt)
            else:
                raise
    return {}


def download_candles(
    inst_id: str,
    bar: str,
    start_ms: int,
    end_ms: int,
) -> list[list[str]]:
    """
    分页下载 K线数据

    OKX API 返回格式: [ts, o, h, l, c, vol, volCcy, volCcyQuote, confirm]
    数据按时间戳降序返回，需要翻转。

    Args:
        inst_id: 交易对，如 "BTC-USDT"
        bar: K线周期，如 "1m"
        start_ms: 起始时间（毫秒时间戳）
        end_ms: 结束时间（毫秒时间戳）

    Returns:
        K线数据列表，每条为 [ts, o, h, l, c, vol, ...]
    """
    all_candles = []
    # OKX 的 after 参数：返回时间戳 < after 的数据（向更早的方向翻页）
    # 我们从 end_ms 开始向前拉取
    cursor = end_ms
    total_fetched = 0

    print(f"  时间范围: {ms_to_str(start_ms)} ~ {ms_to_str(end_ms)} (UTC)")

    while True:
        # 优先使用 history-candles（支持更久远的数据）
        url = (
            f"{BASE_URL}{HISTORY_CANDLES_PATH}"
            f"?instId={inst_id}&bar={bar}&limit={MAX_LIMIT}&after={cursor}"
        )

        data = fetch_json(url)
        candles = data.get("data", [])

        if not candles:
            # history-candles 没数据，尝试 candles 接口（最近 1440 条）
            url = (
                f"{BASE_URL}{CANDLES_PATH}"
                f"?instId={inst_id}&bar={bar}&limit={MAX_LIMIT}&after={cursor}"
            )
            data = fetch_json(url)
            candles = data.get("data", [])
            if not candles:
                break

        # 过滤在范围内的数据
        for c in candles:
            ts = int(c[0])
            if ts >= start_ms:
                all_candles.append(c)

        total_fetched += len(candles)

        # 获取最早一条的时间戳作为下一次的 cursor
        oldest_ts = int(candles[-1][0])

        # 进度显示
        sys.stdout.write(
            f"\r  已下载 {total_fetched} 条 K线，"
            f"当前最早: {ms_to_str(oldest_ts)}"
        )
        sys.stdout.flush()

        # 已经拉取到 start_ms 之前，结束
        if oldest_ts <= start_ms:
            break

        # 继续向前翻页
        cursor = oldest_ts

        # 限流
        time.sleep(REQUEST_INTERVAL)

    print()  # 换行

    # 按时间升序排列
    all_candles.sort(key=lambda x: int(x[0]))

    return all_candles


def candles_to_tick_csv(
    candles: list[list[str]],
    symbol: str,
    output_path: str,
):
    """
    将 K线数据转换为 Tick CSV 格式并写入文件

    CSV 格式: timestamp_ms,symbol,last_price,volume,open,high,low,close
    每根 K线生成一条 Tick 记录，last_price 取收盘价。

    Args:
        candles: OKX K线数据列表 [ts, o, h, l, c, vol, ...]
        symbol: 交易对符号
        output_path: 输出文件路径
    """
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)

    with open(output_path, "w", newline="") as f:
        writer = csv.writer(f)
        # 写入表头（与 CsvLoader 的 Tick 格式一致）
        writer.writerow([
            "timestamp_ms", "symbol", "last_price", "volume",
            "open", "high", "low", "close"
        ])

        for c in candles:
            # OKX K线格式: [ts, o, h, l, c, vol, volCcy, volCcyQuote, confirm]
            ts = c[0]       # 时间戳（毫秒）
            open_ = c[1]    # 开盘价
            high = c[2]     # 最高价
            low = c[3]      # 最低价
            close = c[4]    # 收盘价
            vol = c[5]      # 成交量（交易货币数量）

            writer.writerow([
                ts,           # timestamp_ms
                symbol,       # symbol
                close,        # last_price（使用收盘价）
                vol,          # volume
                open_,        # open
                high,         # high
                low,          # low
                close,        # close
            ])

    print(f"  已写入 {len(candles)} 条数据到: {output_path}")


def parse_date(date_str: str) -> datetime:
    """解析日期字符串为 UTC datetime"""
    return datetime.strptime(date_str, "%Y-%m-%d").replace(tzinfo=timezone.utc)


def main():
    parser = argparse.ArgumentParser(
        description="从 OKX 下载历史 K线数据，转换为回测 Tick CSV 格式",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 下载 BTC-USDT 最近 7 天的 1 分钟 K线
  python3 %(prog)s --symbol BTC-USDT --bar 1m --days 7

  # 下载 ETH-USDT 指定日期范围的 5 分钟 K线
  python3 %(prog)s --symbol ETH-USDT --bar 5m --start 2025-01-01 --end 2025-01-31

  # 下载并指定输出文件
  python3 %(prog)s --symbol BTC-USDT --bar 1H --days 30 --output data/btc_1h.csv

支持的 K线周期: 1m, 3m, 5m, 15m, 30m, 1H, 2H, 4H, 6H, 12H, 1D, 1W, 1M
        """,
    )

    parser.add_argument(
        "--symbol", "-s",
        required=True,
        help="交易对符号，如 BTC-USDT, ETH-USDT",
    )
    parser.add_argument(
        "--bar", "-b",
        default="1m",
        help="K线周期（默认: 1m）",
    )
    parser.add_argument(
        "--days", "-d",
        type=int,
        default=None,
        help="下载最近 N 天的数据（与 --start/--end 二选一）",
    )
    parser.add_argument(
        "--start",
        default=None,
        help="开始日期，格式: YYYY-MM-DD（UTC）",
    )
    parser.add_argument(
        "--end",
        default=None,
        help="结束日期，格式: YYYY-MM-DD（UTC）",
    )
    parser.add_argument(
        "--output", "-o",
        default=None,
        help="输出 CSV 文件路径（默认: data/<symbol>_<bar>.csv）",
    )

    args = parser.parse_args()

    # 确定时间范围
    now = datetime.now(timezone.utc)

    if args.days is not None:
        end_dt = now
        start_dt = now - timedelta(days=args.days)
    elif args.start is not None:
        start_dt = parse_date(args.start)
        end_dt = parse_date(args.end) + timedelta(days=1) if args.end else now
    else:
        # 默认下载最近 7 天
        end_dt = now
        start_dt = now - timedelta(days=7)
        print("  未指定时间范围，默认下载最近 7 天")

    start_ms = ts_to_ms(start_dt)
    end_ms = ts_to_ms(end_dt)

    # 输出文件路径
    if args.output is None:
        safe_symbol = args.symbol.replace("-", "_").lower()
        args.output = f"data/{safe_symbol}_{args.bar}.csv"

    print("=" * 50)
    print("  OKX 历史数据下载")
    print("=" * 50)
    print(f"  交易对:   {args.symbol}")
    print(f"  K线周期:  {args.bar}")
    print(f"  输出文件: {args.output}")
    print()

    # 下载数据
    candles = download_candles(args.symbol, args.bar, start_ms, end_ms)

    if not candles:
        print("  未获取到任何数据，请检查交易对和时间范围")
        sys.exit(1)

    print(f"  共获取 {len(candles)} 条 K线数据")
    print(f"  时间范围: {ms_to_str(int(candles[0][0]))} ~ {ms_to_str(int(candles[-1][0]))}")
    print()

    # 转换并写入 CSV
    candles_to_tick_csv(candles, args.symbol, args.output)

    print()
    print("  下载完成！使用方式：")
    print(f"  ./qitrader --backtest --data-file {args.output}")
    print("=" * 50)


if __name__ == "__main__":
    main()
