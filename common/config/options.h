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
        ("coin,k", po::value<std::string>()->default_value("TRUMP"), "Coin name");
  }

  std::string config_file() {
    return m_vm["config"].as<std::string>();
  }

  std::string coin() {
    return m_vm["coin"].as<std::string>();
  }

private:
  po::options_description m_desc;
  po::variables_map m_vm;
};

}

#define AppOptions Common::SingletonPtr<Config::OptionsImpl>::get_instance()

#endif  // QITRADER_COMMON_CONFIG_OPTIONS_H_
