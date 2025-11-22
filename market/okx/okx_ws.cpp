#include "okx_ws.h"
#include "data.hpp"
#include <glog/logging.h>
#include "utils/utils.h"

namespace market::okx {

// 初始化WebSocket客户端，连接到OKX的WebSocket服务器
OkxWs::OkxWs() {
  if (okx_config->sim()) {
    base_url_ = "wss://wspap.okx.com:8443";
  }
  ws_ = std::make_unique<cpphttp::WebSocket>(base_url_ + uri_);
}

OkxWs::OkxWs(std::string uri) {
  uri_ = uri;
  if (okx_config->sim()) {
    base_url_ = "wss://wspap.okx.com:8443";
  }
  ws_ = std::make_unique<cpphttp::WebSocket>(base_url_ + uri_);
}

OkxWs::~OkxWs() {}

// 连接到WebSocket服务器
asio::awaitable<void> OkxWs::connect() {
  LOG(INFO) << "Connecting to OKX WebSocket server url: " << base_url_ + uri_;
  co_await ws_->connect();
  co_return;
}

// 从WebSocket读取消息并解析为WsMessage结构
asio::awaitable<market::okx::WsMessage> OkxWs::read() {
  // 读取原始JSON数据
  auto rsp = co_await ws_->read();
  // 解析JSON为WsMessage结构体
  auto msg = *jsoncpp::from_json<market::okx::WsMessage>(rsp);
  co_return msg;
}

std::string get_sign(
  std::string timestamp,
  std::string secret_key
) {
  auto sign_str = timestamp + "GET" + "/users/self/verify";
  auto sign = Common::sha256_hash_base64(sign_str, secret_key);
  return sign;
}

}  // namespace market::okx
