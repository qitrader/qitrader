#ifndef QITRADER_COMMON_CONFIG_OPTIONS_H_
#define QITRADER_COMMON_CONFIG_OPTIONS_H_

#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <iostream>
#include "utils/errcode.h"
#include "utils/utils.h"

namespace Config {

namespace po = boost::program_options;

class OptionsImpl {
public:
  OptionsImpl() = default;

  ErrCode operator()(int argc, char* argv[]) {
    init_desc();
    try {
      po::store(po::parse_command_line(argc, argv, m_desc), m_vm);
      po::notify(m_vm);
    } catch (const po::error& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      std::cerr << m_desc << std::endl;
      return ErrCode::Invalid_Param;
    }
    return ErrCode::OK;
  }

  void init_desc() {
    m_desc.add_options()
        ("help,h", "Show help message")
        ("config,c", po::value<std::string>()->default_value("config.ini"), "Path to config file")
        ("log,l", po::value<std::string>(), "Path to log file")
        ("coin,k", po::value<std::string>()->default_value("TRUMP"), "Coin name")
        ("backtest", "Enable backtest mode")
        ("paper", "Enable paper trading mode (live market data + virtual trading)")
        ("venue", po::value<std::string>()->default_value("auto"),
         "Execution venue: auto, gateway or backtest")
        ("initial-capital", po::value<std::string>()->default_value("10000"), "Initial virtual capital for paper/backtest mode")
        ("report-interval", po::value<int>()->default_value(60), "Paper mode account summary interval in seconds")
        ("record-file", po::value<std::string>()->default_value(""),
         "Record live market data (with order book) to CSV for offline replay training")
        ("model-path", po::value<std::string>()->default_value(""),
         "Path to save/load RL model weights (multilevel), empty disables persistence")
        ("maker-fee-rate", po::value<std::string>()->default_value("0.0008"),
         "Maker (limit) fee rate for paper/backtest, 0.0008 = 0.08% (OKX spot Lv1)")
        ("taker-fee-rate", po::value<std::string>()->default_value("0.001"),
         "Taker (market) fee rate for paper/backtest, 0.001 = 0.1% (OKX spot Lv1)")
        ("strategy", po::value<std::string>()->default_value("testing"), "Strategy to use: testing, grid, multilevel")
        ("data-file", po::value<std::string>(), "Path to historical data CSV file")
        ("start-date", po::value<std::string>(), "Backtest start date (YYYY-MM-DD)")
        ("end-date", po::value<std::string>(), "Backtest end date (YYYY-MM-DD)")
        ("symbol", po::value<std::string>()->default_value("BTC-USDT-SWAP"), "Trading symbol for strategy")
        ("grid-upper", po::value<std::string>(), "Grid upper bound price")
        ("grid-lower", po::value<std::string>(), "Grid lower bound price")
        ("grid-count", po::value<int>()->default_value(10), "Number of grid levels")
        ("grid-amount", po::value<std::string>()->default_value("0.01"), "Order amount per grid level")
        ("mm-levels", po::value<int>()->default_value(3), "Market making price levels")
        ("mm-order-budget", po::value<int>()->default_value(20), "Market making order lots per decision")
        ("mm-order-size", po::value<std::string>()->default_value("1"), "Market making size per lot")
        ("mm-inventory-limit", po::value<std::string>()->default_value("20"), "Market making inventory limit")
        ("mm-decision-interval-ms", po::value<int64_t>()->default_value(30000), "Market making decision interval in milliseconds")
        ("mm-inventory-penalty", po::value<double>()->default_value(0.01), "Market making inventory penalty")
        ("mm-learning-rate", po::value<double>()->default_value(0.0005), "Market making actor-critic learning rate")
        ("mm-exploration", po::value<double>()->default_value(0.05), "Market making exploration strength")
        ("mm-min-half-spread-bps", po::value<double>()->default_value(0.0), "Minimum half spread in bps so a round trip covers fees")
        ("mm-allow-market-orders", po::value<int>()->default_value(0), "Allow taker market orders, 1 enables (usually unprofitable)")
        ("mm-inventory-skew-bps", po::value<double>()->default_value(0.0), "Inventory skew strength in bps applied to reservation price");
  }

  std::string config_file() {
    return m_vm["config"].as<std::string>();
  }

  /// 是否请求显示帮助信息
  bool is_help() const {
    return m_vm.count("help") > 0;
  }

  /// 输出命令行帮助信息
  void show_help(std::ostream& os = std::cout) const {
    os << m_desc << std::endl;
  }

  std::string coin() {
    return m_vm["coin"].as<std::string>();
  }

  /// 是否为回测模式
  bool is_backtest() const {
    return m_vm.count("backtest") > 0;
  }

  /// 获取历史数据文件路径
  std::string data_file() const {
    if (m_vm.count("data-file")) {
      return m_vm["data-file"].as<std::string>();
    }
    return "";
  }

  /// 获取回测开始日期
  std::string start_date() const {
    if (m_vm.count("start-date")) {
      return m_vm["start-date"].as<std::string>();
    }
    return "";
  }

  /// 获取回测结束日期
  std::string end_date() const {
    if (m_vm.count("end-date")) {
      return m_vm["end-date"].as<std::string>();
    }
    return "";
  }

  /// 是否为模拟交易模式（Paper Trading）
  bool is_paper() const {
    return m_vm.count("paper") > 0;
  }

  /**
   * @brief 执行端口选择。
   *
   * `auto` 表示按运行模式推断：回测使用回测撮合端口，
   * Paper 和实盘使用网关端口。
   */
  std::string venue() const {
    return m_vm["venue"].as<std::string>();
  }

  /// 解析后的执行端口名称，把 `auto` 展开为具体端口。
  std::string resolved_venue() const {
    const std::string name = venue();
    if (name != "auto") return name;
    return is_backtest() ? "backtest" : "gateway";
  }

  /// 获取初始虚拟资金
  std::string initial_capital() const {
    return m_vm["initial-capital"].as<std::string>();
  }

  /// 获取模拟交易账户摘要输出间隔（秒）
  int report_interval() const {
    return m_vm["report-interval"].as<int>();
  }

  /// 获取行情录制路径，为空表示不录制
  std::string record_file() const {
    return m_vm["record-file"].as<std::string>();
  }

  /// 获取模型落盘路径，为空表示不做持久化
  std::string model_path() const {
    return m_vm["model-path"].as<std::string>();
  }

  /// 获取挂单成交（maker）手续费率
  std::string maker_fee_rate() const {
    return m_vm["maker-fee-rate"].as<std::string>();
  }

  /// 获取吃单成交（taker）手续费率
  std::string taker_fee_rate() const {
    return m_vm["taker-fee-rate"].as<std::string>();
  }

  /// 获取策略名称
  std::string strategy_name() const {
    return m_vm["strategy"].as<std::string>();
  }

  /// 获取交易对
  std::string symbol() const {
    return m_vm["symbol"].as<std::string>();
  }

  /// 获取网格上界价格
  std::string grid_upper() const {
    if (m_vm.count("grid-upper")) {
      return m_vm["grid-upper"].as<std::string>();
    }
    return "";
  }

  /// 获取网格下界价格
  std::string grid_lower() const {
    if (m_vm.count("grid-lower")) {
      return m_vm["grid-lower"].as<std::string>();
    }
    return "";
  }

  /// 获取网格数量
  int grid_count() const {
    return m_vm["grid-count"].as<int>();
  }

  /// 获取每格下单数量
  std::string grid_amount() const {
    return m_vm["grid-amount"].as<std::string>();
  }

  int mm_levels() const { return m_vm["mm-levels"].as<int>(); }
  int mm_order_budget() const { return m_vm["mm-order-budget"].as<int>(); }
  std::string mm_order_size() const { return m_vm["mm-order-size"].as<std::string>(); }
  std::string mm_inventory_limit() const { return m_vm["mm-inventory-limit"].as<std::string>(); }
  int64_t mm_decision_interval_ms() const {
    return m_vm["mm-decision-interval-ms"].as<int64_t>();
  }
  double mm_inventory_penalty() const { return m_vm["mm-inventory-penalty"].as<double>(); }
  double mm_learning_rate() const { return m_vm["mm-learning-rate"].as<double>(); }
  double mm_exploration() const { return m_vm["mm-exploration"].as<double>(); }
  double mm_min_half_spread_bps() const { return m_vm["mm-min-half-spread-bps"].as<double>(); }
  int mm_allow_market_orders() const { return m_vm["mm-allow-market-orders"].as<int>(); }
  double mm_inventory_skew_bps() const { return m_vm["mm-inventory-skew-bps"].as<double>(); }

private:
  po::options_description m_desc;
  po::variables_map m_vm;
};

}

#define AppOptions Common::SingletonPtr<Config::OptionsImpl>::get_instance()

#endif  // QITRADER_COMMON_CONFIG_OPTIONS_H_
