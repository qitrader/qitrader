#ifndef _MARKET_OKX_OKX_WS_H_
#define _MARKET_OKX_OKX_WS_H_

#include <string>
#include <vector>

#include "data.hpp"
#include <memory>
#include "httpcpp/WebSocket.h"

namespace market::okx {

class OkxWs {
 public:
  OkxWs();
  OkxWs(std::string uri);
  ~OkxWs();
  asio::awaitable<void> connect();
  asio::awaitable<market::okx::WsMessage> read();

  template <typename T>
  asio::awaitable<void> write(T&& message) {
    auto msg_str = jsoncpp::to_json(message);
    co_await ws_->write(msg_str);
  }

 private:
  std::unique_ptr<cpphttp::WebSocket> ws_;
  std::string uri_ = "/ws/v5/public";
  std::string base_url_ = "wss://ws.okx.com:8443";
};

std::string get_sign(
  std::string timestamp,
  std::string secret_key
);

}  // namespace market::okx

#endif  // _MARKET_OKX_OKX_WS_H_
