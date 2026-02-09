#ifndef QITRADER_NOTICE_WEWORK_WEWORK_H_
#define QITRADER_NOTICE_WEWORK_WEWORK_H_
#include <boost/asio/awaitable.hpp>
#include <string>

#include "base/notice.h"
#include "config/config.h"

namespace notice {
namespace wework {

namespace asio = boost::asio;

class WeWorkConfig : public Config::ConfigTree {
 public:
  WeWorkConfig() : ConfigTree("wework") {}

  void load(std::shared_ptr<Config::ptree> pt) override {
    m_ptree = pt;
    m_key = this->get<std::string>("key");
  }

  std::string key() const { return m_key; }

 private:
  std::string m_key;
};

#define wework_config ::Common::SingletonPtr<::notice::wework::WeWorkConfig>::get_instance()

class WeworkText {
 public:
  std::string content;
};

class WeworkData {
 public:
  std::string msgtype = "text";
  WeworkText text;
};

class WeworkNotice : public notice::base::Notice {
 public:
  WeworkNotice(engine::EnginePtr engine);
  asio::awaitable<void> send_message(engine::MessageDataPtr msg) override;
  asio::awaitable<void> run() override;

 private:
  std::string m_base_uri = "https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=";
  std::string m_key = "";
  std::string m_uri = "";
};

}  // namespace wework
}  // namespace notice

#endif
