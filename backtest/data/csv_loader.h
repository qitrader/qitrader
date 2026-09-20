#ifndef QITRADER_BACKTEST_DATA_CSV_LOADER_H_
#define QITRADER_BACKTEST_DATA_CSV_LOADER_H_

/**
 * @file csv_loader.h
 * @brief CSV 历史数据加载器
 *
 * 支持从 CSV 文件加载 Tick 数据和 K线数据，
 * 按时间戳升序排列，支持日期范围过滤。
 */

#include <fstream>
#include <string>
#include <vector>

#include "object.h"

namespace backtest::data {

/**
 * @brief CSV 数据加载器
 *
 * 支持两种 CSV 格式：
 * - Tick: timestamp_ms,symbol,last_price,volume,open,high,low,close
 * - Bar:  timestamp_ms,symbol,open,high,low,close,volume
 */
class CsvLoader {
 public:
  /**
   * @brief 构造函数
   * @param file_path CSV 文件路径
   * @param start_date 开始日期（YYYY-MM-DD），为空则不过滤
   * @param end_date 结束日期（YYYY-MM-DD），为空则不过滤
   */
  CsvLoader(const std::string& file_path, const std::string& start_date = "",
            const std::string& end_date = "");

  /**
   * @brief 加载 Tick 数据
   * @return std::vector<std::shared_ptr<engine::TickData>> 按时间升序排列的 Tick 数据
   */
  std::vector<std::shared_ptr<engine::TickData>> loadTicks();

  /**
   * @brief 加载 K线数据
   * @return std::vector<std::shared_ptr<engine::BarData>> 按时间升序排列的 K线数据
   */
  std::vector<std::shared_ptr<engine::BarData>> loadBars();

  /**
   * @brief 检查文件是否可读
   * @return bool 文件是否存在且可读
   */
  bool isValid() const;

 private:
  /// 将日期字符串（YYYY-MM-DD）转换为毫秒时间戳
  int64_t dateToTimestampMs(const std::string& date) const;

  /// 检查时间戳是否在过滤范围内
  bool inRange(int64_t timestamp_ms) const;

  /// 去除字符串首尾空白
  std::string trim(const std::string& s) const;

  std::string m_file_path;      ///< CSV 文件路径
  int64_t m_start_ms;           ///< 过滤开始时间戳（毫秒），0 表示不过滤
  int64_t m_end_ms;             ///< 过滤结束时间戳（毫秒），0 表示不过滤
};

}  // namespace backtest::data

#endif  // QITRADER_BACKTEST_DATA_CSV_LOADER_H_
