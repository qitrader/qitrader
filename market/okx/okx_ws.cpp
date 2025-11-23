#include "okx_ws.h"

#include <glog/logging.h>

#include "data.hpp"
#include "utils/utils.h"

namespace market::okx {

// 初始化WebSocket客户端，连接到OKX的WebSocket服务器
OkxWs::OkxWs(boost::asio::any_io_executor& ctx, size_t channel_size)
    : write_channel_(ctx, channel_size), read_channel_(ctx, channel_size) {
  if (okx_config->sim()) {
    base_url_ = "wss://wspap.okx.com:8443";
  }
  ws_ = std::make_unique<cpphttp::WebSocket>(base_url_ + uri_);
}

OkxWs::OkxWs(boost::asio::any_io_executor& ctx, size_t channel_size, std::string uri)
    : write_channel_(ctx, channel_size), read_channel_(ctx, channel_size) {
  uri_ = uri;
  if (okx_config->sim()) {
    base_url_ = "wss://wspap.okx.com:8443";
  }
  ws_ = std::make_unique<cpphttp::WebSocket>(base_url_ + uri_);
}

OkxWs::~OkxWs() {}

// 连接到WebSocket服务器
asio::awaitable<void> OkxWs::connect() {
  auto ctx = co_await asio::this_coro::executor;

  if (okx_config->sim()) {
    ws_->add_header("x-simulated-trading", "1");
  }

  ws_->add_header("User-Agent", "qitrader");

  co_await ws_->connect();

  co_spawn(
      ctx, [this] { return read_loop(); }, asio::detached);
  co_spawn(
      ctx, [this] { return write_loop(); }, asio::detached);

  co_return;
}

// 从WebSocket读取消息并解析为WsMessage结构
asio::awaitable<market::okx::WsMessage> OkxWs::read() {
  // 读取原始JSON数据
  auto rsp = co_await read_channel_.async_receive();
  co_return rsp;
}

asio::awaitable<void> OkxWs::read_loop() {
  while (true) {
    try {
      auto rsp = co_await ws_->read();
      auto msg = jsoncpp::from_json<market::okx::WsMessage>(rsp);
      co_await read_channel_.async_send(boost::system::error_code{}, *msg, asio::use_awaitable);
    } catch (const boost::system::error_code& e) {
      LOG(ERROR) << fmt::format("{} Error in read_loop: code {} {}", uri_, e.value(), e.what());
    } catch (const std::exception& e) {
      LOG(ERROR) << fmt::format("{} Error in read_loop: {}", uri_, e.what());
    } catch (...) {
      LOG(ERROR) << fmt::format("{} Unknown error in read_loop", uri_);
    }
  }
}

asio::awaitable<void> OkxWs::write_loop() {
  while (true) {
    try {
      auto msg = co_await write_channel_.async_receive();
      co_await ws_->write(msg);
    } catch (const boost::system::error_code& e) {
      LOG(ERROR) << fmt::format("{} Error in write_loop: code {} {}", uri_, e.value(), e.what());
    } catch (const std::exception& e) {
      LOG(ERROR) << fmt::format("{} Error in write_loop: {}", uri_, e.what());
    } catch (...) {
      LOG(ERROR) << fmt::format("{} Unknown error in write_loop", uri_);
    }
  }
}

std::string get_sign(std::string timestamp, std::string secret_key) {
  auto sign_str = timestamp + "GET" + "/users/self/verify";
  auto sign = Common::sha256_hash_base64(sign_str, secret_key);
  return sign;
}

}  // namespace market::okx
