#ifndef QITRADER_MARKET_OKX_OKX_WS_H_
#define QITRADER_MARKET_OKX_OKX_WS_H_
#include <atomic>
#include <string>
#include <vector>

#include "data.hpp"
#include <memory>
#include "httpcpp/WebSocket.h"
#include <glog/logging.h>
#include <boost/asio/experimental/concurrent_channel.hpp>

namespace market::okx {

class OkxWs : public std::enable_shared_from_this<OkxWs> {
 public:
  OkxWs(boost::asio::any_io_executor& ctx, size_t channel_size);
  OkxWs(boost::asio::any_io_executor& ctx, size_t channel_size, std::string uri);
  ~OkxWs();
  asio::awaitable<void> connect();
  asio::awaitable<market::okx::WsMessage> read();

  /**
   * @brief 中断挂起的 `read()`，使其立即返回错误。
   *
   * WebSocket 可能在不报错的情况下停止推送（连接仍是 ESTABLISHED，
   * 内核缓冲区还有数据），此时 `read()` 会一直挂起而无法触发重连。
   * 看门狗用它把连接打断，交给上层重建。
   */
  void interrupt();

  template <typename T>
  asio::awaitable<void> write(T&& message) {
    auto msg_str = jsoncpp::to_json(message);
    co_await write_channel_.async_send(boost::system::error_code{}, msg_str, asio::use_awaitable);
  }

 private:
  asio::awaitable<void> read_loop();
  asio::awaitable<void> write_loop();

  /// 退避一段时间，避免连接异常时以 CPU 速度重试刷爆日志
  asio::awaitable<void> backoff(int errors);

  /// 连续错误达到该次数后退出读写循环，避免无限错误风暴
  static constexpr int kMaxConsecutiveErrors = 5;

  /// 连接是否已作废（被看门狗中断或错误过多），用于让读写循环退出
  std::atomic<bool> m_stopped{false};

  std::unique_ptr<cpphttp::WebSocket> ws_;
  std::string uri_ = "/ws/v5/public";
  std::string base_url_ = "wss://ws.okx.com:8443";

  /// 保存 executor，供 interrupt() 派生关闭底层连接的协程
  boost::asio::any_io_executor executor_;

  asio::experimental::concurrent_channel<void(boost::system::error_code, market::okx::WsMessage)> read_channel_;
  asio::experimental::concurrent_channel<void(boost::system::error_code, std::string)> write_channel_;
};

std::string get_sign(
  std::string timestamp,
  std::string secret_key
);

}  // namespace market::okx

#endif  // _MARKET_OKX_OKX_WS_H_
