#ifndef QITRADER_COMMON_CONFIG_CONFIG_H_
#define QITRADER_COMMON_CONFIG_CONFIG_H_

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <memory>
#include <string>

#include "utils/utils.h"

namespace Config {

using ptree = boost::property_tree::ptree;
using dec_float = boost::multiprecision::cpp_dec_float_100;

class ConfigTree {
 public:
  ConfigTree(const std::string& prefix) : m_prefix(prefix){};

  template <typename T>
  T get(const std::string& key) {
    return m_ptree->get<T>(m_prefix + "." + key);
  }

  virtual void load(std::shared_ptr<Config::ptree> pt) = 0;

 protected:
  std::shared_ptr<ptree> m_ptree;

 private:
  std::string m_prefix;
};

class CommonConfig : public ConfigTree {
 public:
  CommonConfig() : ConfigTree("common") {};

  void load(std::shared_ptr<Config::ptree> pt) override {
    m_ptree = pt;
    m_timeout_ms = this->get<uint32_t>("timeout_ms");
  }

  uint32_t timeout_ms() const { return m_timeout_ms; }

 private:
  uint32_t m_timeout_ms;
};

#define common_config ::Common::SingletonPtr<::Config::CommonConfig>::get_instance()

class Config {
 public:
  Config(const std::string& config_file = "") {
    if (!config_file.empty()) {
      init(config_file);
    }
  };

  void init(const std::string& config_file = "config.ini") {
    m_ptree = std::make_shared<ptree>();
    boost::property_tree::read_ini(config_file, *m_ptree);
  }

  void load_config(
    std::initializer_list<std::shared_ptr<ConfigTree>> configs
  ) {
    for (auto& config : configs) {
      config->load(m_ptree);
    }
  }

 private:
  std::shared_ptr<ptree> m_ptree;
};

};  // namespace Config

#define AppConfig Common::SingletonPtr<Config::Config>::get_instance()

#endif  // QITRADER_COMMON_CONFIG_CONFIG_H_
