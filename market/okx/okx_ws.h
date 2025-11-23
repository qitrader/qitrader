#ifndef _MARKET_OKX_OKX_WS_H_
#define _MARKET_OKX_OKX_WS_H_

#include <string>
#include <vector>

#include "data.hpp"
#include <memory>
#include "httpcpp/WebSocket.h"
#include <glog/logging.h>
#include <boost/asio/experimental/concurrent_channel.hpp>

namespace market::okx {

class OkxWs {
 public:
  OkxWs(boost::asio::any_io_executor& ctx, size_t channel_size);
  OkxWs(boost::asio::any_io_executor& ctx, size_t channel_size, std::string uri);
  ~OkxWs();
  asio::awaitable<void> connect();
  asio::awaitable<market::okx::WsMessage> read();

  template <typename T>
  asio::awaitable<void> write(T&& message) {
    auto msg_str = jsoncpp::to_json(message);
    co_await write_channel_.async_send(boost::system::error_code{}, msg_str, asio::use_awaitable);
  }

 private:
  asio::awaitable<void> read_loop();
  asio::awaitable<void> write_loop();

  std::unique_ptr<cpphttp::WebSocket> ws_;
  std::string uri_ = "/ws/v5/public";
  std::string base_url_ = "wss://ws.okx.com:8443";

  asio::experimental::concurrent_channel<void(boost::system::error_code, market::okx::WsMessage)> read_channel_;
  asio::experimental::concurrent_channel<void(boost::system::error_code, std::string)> write_channel_;
};

std::string get_sign(
  std::string timestamp,
  std::string secret_key
);

}  // namespace market::okx

#endif  // _MARKET_OKX_OKX_WS_H_
