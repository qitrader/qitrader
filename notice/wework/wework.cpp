#include "wework.h"

#include <httpcpp/request.h>
#include <boost/json.hpp>
#include "utils/utils.h"

namespace notice::wework {

WeworkNotice::WeworkNotice(engine::EnginePtr engine) : 
  notice::base::Notice(engine), m_key(wework_config->key()), m_uri(m_base_uri + wework_config->key()) {}

asio::awaitable<void> WeworkNotice::send_message(engine::MessageDataPtr msg) {
  // 引擎回调里消息类型不匹配时 dynamic_pointer_cast 会失败，msg 可能为空，
  // 直接解引用会让整个引擎事件循环崩溃。通知是旁路功能，不能反噬主流程。
  if (!msg) {
    LOG(WARNING) << "企业微信通知忽略: 消息指针为空";
    co_return;
  }

  auto send_obj = WeworkData();
  send_obj.text.content = msg->message;

  auto snd_msg = boost::json::serialize(jsoncpp::to_json(send_obj));

  LOG(INFO) << fmt::format("send msg req: {}", snd_msg);

  auto req = cpphttp::HttpRequest(
    m_uri, "POST", snd_msg
  );

  // request() 对非 200 响应会抛 std::runtime_error（HTTP 失败、超时等），
  // 异常冒泡出协程会中断引擎回调链，后续通知全部静默丢失。
  // 这里就地兜住并记日志：单条通知失败不影响引擎继续运行。
  try {
    auto resp = co_await req.request();
    LOG(INFO) << fmt::format("send msg rsp: {}", resp);
  } catch (const std::exception& e) {
    LOG(ERROR) << fmt::format("企业微信通知发送失败: {}", e.what());
  } catch (...) {
    LOG(ERROR) << "企业微信通知发送失败: 未知异常";
  }

  co_return;
}

asio::awaitable<void> WeworkNotice::run() {
  co_return;
}


}  // namespace notice::wework
