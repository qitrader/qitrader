#include "wework.h"

#include <httpcpp/request.h>
#include "utils/component.hpp"
#include <boost/json.hpp>
#include "utils/utils.h"

namespace notice::wework {

WeworkNotice::WeworkNotice(engine::EnginePtr engine) : 
  notice::base::Notice(engine), m_key(wework_config->key()), m_uri(m_base_uri + wework_config->key()) {}

asio::awaitable<void> WeworkNotice::send_message(engine::MessageDataPtr msg) {
  auto send_obj = WeworkData();
  send_obj.text.content = msg->message;

  auto snd_msg = boost::json::serialize(jsoncpp::to_json(send_obj));

  LOG(INFO) << fmt::format("send msg req: {}", snd_msg);

  auto req = cpphttp::HttpRequest(
    m_uri, "POST", snd_msg
  );

  auto resp = co_await req.request();
  LOG(INFO) << fmt::format("send msg rsp: {}", resp);
  
  co_return;
}

asio::awaitable<void> WeworkNotice::run() {
  co_return;
}


}  // namespace notice::wework
