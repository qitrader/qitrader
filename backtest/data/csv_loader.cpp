#include "csv_loader.h"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace backtest::data {

namespace {

// 公历日期 -> 1970-01-01 起的天数（days-from-civil 算法）。
// 纯整数运算，不依赖进程时区与 DST，也不依赖 timegm 这类非标准接口，
// 结果等价于该日期 UTC 零点的日序号。
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = static_cast<unsigned>(year - era * 400);  // [0, 399]
  const unsigned day_of_year =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;          // [0, 365]
  const unsigned day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;  // [0, 146096]
  return era * 146097 + static_cast<int64_t>(day_of_era) - 719468;
}

}  // namespace

CsvLoader::CsvLoader(const std::string& file_path, const std::string& start_date,
                     const std::string& end_date)
    : m_file_path(file_path), m_start_ms(0), m_end_ms(0) {
  if (!start_date.empty()) {
    m_start_ms = dateToTimestampMs(start_date);
  }
  if (!end_date.empty()) {
    // 结束日期设为当天 23:59:59.999
    m_end_ms = dateToTimestampMs(end_date) + 86400000 - 1;
  }
}

bool CsvLoader::isValid() const {
  std::ifstream file(m_file_path);
  return file.good();
}

std::vector<std::shared_ptr<engine::TickData>> CsvLoader::loadTicks() {
  std::vector<std::shared_ptr<engine::TickData>> ticks;

  std::ifstream file(m_file_path);
  if (!file.is_open()) {
    LOG(ERROR) << "无法打开数据文件: " << m_file_path;
    return ticks;
  }

  std::string line;
  // 跳过表头
  if (!std::getline(file, line)) {
    LOG(ERROR) << "数据文件为空: " << m_file_path;
    return ticks;
  }

  while (std::getline(file, line)) {
    if (line.empty()) continue;

    std::istringstream ss(line);
    std::string token;
    std::vector<std::string> fields;

    while (std::getline(ss, token, ',')) {
      fields.push_back(trim(token));
    }

    // Tick 格式: timestamp_ms,symbol,last_price,volume,open,high,low,close
    if (fields.size() < 8) {
      LOG(WARNING) << "跳过格式错误的行: " << line;
      continue;
    }

    try {
      int64_t ts = std::stoll(fields[0]);
      if (!inRange(ts)) continue;

      auto tick = std::make_shared<engine::TickData>();
      tick->timestamp_ms = ts;
      tick->symbol = fields[1];
      tick->last_price = dec_float(fields[2]);
      tick->last_volume = dec_float(fields[3]);
      tick->open_price = dec_float(fields[4]);
      tick->high_price = dec_float(fields[5]);
      tick->low_price = dec_float(fields[6]);
      tick->last_close_price = dec_float(fields[7]);
      tick->exchange = "backtest";

      // 可选扩展列：5 档盘口（买价,买量,卖价,卖量），由录制产生。
      // 没有这些列时保持原行为，策略会退回到合成盘口。
      constexpr std::size_t kBaseFields = 8;
      constexpr std::size_t kBookLevels = 5;
      constexpr std::size_t kBookFields = kBookLevels * 4;
      if (fields.size() >= kBaseFields + kBookFields) {
        auto book = std::make_shared<engine::Book>();
        book->symbol = tick->symbol;
        book->exchange = "backtest";
        book->timestamp_ms = ts;
        for (std::size_t level = 0; level < kBookLevels; ++level) {
          const std::size_t base = kBaseFields + level * 4;
          engine::BookItem bid;
          bid.symbol = tick->symbol;
          bid.price = dec_float(fields[base]);
          bid.volume = dec_float(fields[base + 1]);
          book->bids.push_back(bid);

          engine::BookItem ask;
          ask.symbol = tick->symbol;
          ask.price = dec_float(fields[base + 2]);
          ask.volume = dec_float(fields[base + 3]);
          book->asks.push_back(ask);
        }
        tick->order_book = book;
      }
      ticks.push_back(tick);
    } catch (const std::exception& e) {
      LOG(WARNING) << "跳过数据格式错误的 Tick 行: " << e.what();
    }
  }

  // 按时间戳升序排序
  std::sort(ticks.begin(), ticks.end(),
            [](const auto& a, const auto& b) { return a->timestamp_ms < b->timestamp_ms; });

  LOG(INFO) << fmt::format("加载 {} 条 Tick 数据", ticks.size());
  return ticks;
}

std::vector<std::shared_ptr<engine::BarData>> CsvLoader::loadBars() {
  std::vector<std::shared_ptr<engine::BarData>> bars;

  std::ifstream file(m_file_path);
  if (!file.is_open()) {
    LOG(ERROR) << "无法打开数据文件: " << m_file_path;
    return bars;
  }

  std::string line;
  // 跳过表头
  if (!std::getline(file, line)) {
    LOG(ERROR) << "数据文件为空: " << m_file_path;
    return bars;
  }

  while (std::getline(file, line)) {
    if (line.empty()) continue;

    std::istringstream ss(line);
    std::string token;
    std::vector<std::string> fields;

    while (std::getline(ss, token, ',')) {
      fields.push_back(trim(token));
    }

    // Bar 格式: timestamp_ms,symbol,open,high,low,close,volume
    if (fields.size() < 7) {
      LOG(WARNING) << "跳过格式错误的行: " << line;
      continue;
    }

    try {
      int64_t ts = std::stoll(fields[0]);
      if (!inRange(ts)) continue;

      auto bar = std::make_shared<engine::BarData>();
      bar->timestamp_ms = ts;
      bar->symbol = fields[1];
      bar->open_price = dec_float(fields[2]);
      bar->high_price = dec_float(fields[3]);
      bar->low_price = dec_float(fields[4]);
      bar->close_price = dec_float(fields[5]);
      bar->volume = dec_float(fields[6]);
      bar->exchange = "backtest";
      bars.push_back(bar);
    } catch (const std::exception& e) {
      LOG(WARNING) << "跳过数据格式错误的 K 线行: " << e.what();
    }
  }

  // 按时间戳升序排序
  std::sort(bars.begin(), bars.end(),
            [](const auto& a, const auto& b) { return a->timestamp_ms < b->timestamp_ms; });

  LOG(INFO) << fmt::format("加载 {} 条 K线数据", bars.size());
  return bars;
}

int64_t CsvLoader::dateToTimestampMs(const std::string& date) const {
  std::tm tm = {};
  std::istringstream ss(date);
  ss >> std::get_time(&tm, "%Y-%m-%d");
  if (ss.fail()) {
    LOG(ERROR) << "日期格式错误: " << date << "，应为 YYYY-MM-DD";
    return 0;
  }
  // 不能直接用 std::mktime：mktime 按进程本地时区解释 tm，而 CSV 里的
  // timestamp_ms 是 UTC 毫秒时间戳。在 UTC+8 环境下 "2024-01-01" 会被折算成
  // 2023-12-31T16:00:00Z，--start-date/--end-date 的过滤边界整体偏移 8 小时，
  // 首尾数据被误过滤/误保留。std::get_time 只负责解析字段，UTC epoch 换算
  // 由 daysFromCivil() 完成，与平台和时区无关。
  const int year = tm.tm_year + 1900;
  const unsigned month = static_cast<unsigned>(tm.tm_mon) + 1;  // tm_mon: 0~11
  const unsigned day = static_cast<unsigned>(tm.tm_mday);       // tm_mday: 1~31
  if (month < 1 || month > 12 || day < 1 || day > 31) {
    LOG(ERROR) << "日期取值非法: " << date << "，应为 YYYY-MM-DD";
    return 0;
  }
  return daysFromCivil(year, month, day) * 86400000;
}

bool CsvLoader::inRange(int64_t timestamp_ms) const {
  if (m_start_ms > 0 && timestamp_ms < m_start_ms) return false;
  if (m_end_ms > 0 && timestamp_ms > m_end_ms) return false;
  return true;
}

std::string CsvLoader::trim(const std::string& s) const {
  auto start = s.find_first_not_of(" \t\r\n");
  auto end = s.find_last_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  return s.substr(start, end - start + 1);
}

}  // namespace backtest::data
