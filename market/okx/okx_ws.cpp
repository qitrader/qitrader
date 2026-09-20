#include "okx_ws.h"

#include <glog/logging.h>

#include "data.hpp"
#include "utils/utils.h"

namespace market::okx {

// 初始化WebSocket客户端，连接到OKX的WebSocket服务器
OkxWs::OkxWs(boost::asio::any_io_executor& ctx, size_t channel_size)
    : write_channel_(ctx, channel_size), read_channel_(ctx, channel_size), executor_(ctx) {
  if (okx_config->sim()) {
    base_url_ = "wss://wspap.okx.com:8443";
  }
  ws_ = std::make_unique<cpphttp::WebSocket>(base_url_ + uri_);
}

OkxWs::OkxWs(boost::asio::any_io_executor& ctx, size_t channel_size, std::string uri)
    : write_channel_(ctx, channel_size), read_channel_(ctx, channel_size), executor_(ctx) {
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

  auto self = shared_from_this();
  co_spawn(
      ctx, [self] { return self->read_loop(); }, asio::detached);
  co_spawn(
      ctx, [self] { return self->write_loop(); }, asio::detached);

  co_return;
}

// 从WebSocket读取消息并解析为WsMessage结构
asio::awaitable<market::okx::WsMessage> OkxWs::read() {
  // 读取原始JSON数据
  auto rsp = co_await read_channel_.async_receive();
  co_return rsp;
}

void OkxWs::interrupt() {
  // 关闭读取通道，让挂起的 read() 带着错误立即返回。
  // 同时置停止标志：否则 read_loop 会在通道关闭后持续拿到错误并立即重试，
  // 形成错误风暴，几秒就能刷出数 GB 日志。
  m_stopped.store(true);
  read_channel_.close();
  write_channel_.close();

  // 底层 TCP 连接也必须关闭：只关通道的话，对端发来 FIN 后 socket 会一直停在
  // CLOSE-WAIT，每次重连泄漏一个 fd（线上曾累积到 5 个）。close() 是协程，
  // 这里捕获 shared_from_this() 保证关闭期间 ws_ 仍然存活。
  if (ws_) {
    auto self = shared_from_this();
    asio::co_spawn(
        executor_,
        [self]() -> asio::awaitable<void> {
          try {
            co_await self->ws_->close();
          } catch (...) {
            // 连接本就处于废弃流程，关闭失败无需处理
          }
        },
        asio::detached);
  }
}

asio::awaitable<void> OkxWs::backoff(int errors) {
  asio::steady_timer timer(co_await asio::this_coro::executor);
  timer.expires_after(std::chrono::milliseconds(200 * errors));
  co_await timer.async_wait(asio::use_awaitable);
}

asio::awaitable<void> OkxWs::read_loop() {
  int errors = 0;
  while (!m_stopped.load() && errors < kMaxConsecutiveErrors) {
    std::string errmsg;
    try {
      auto rsp = co_await ws_->read();
      errors = 0;  // 成功读取即重置连续错误计数
      auto msg = jsoncpp::from_json<market::okx::WsMessage>(rsp);
      co_await read_channel_.async_send(boost::system::error_code{}, *msg, asio::use_awaitable);
      continue;
    } catch (const boost::system::error_code& e) {
      errmsg = fmt::format("code {} {}", e.value(), e.what());
    } catch (const std::exception& e) {
      errmsg = e.what();
    } catch (...) {
      errmsg = "unknown error";
    }

    // 看门狗主动中断时 m_stopped 已置位，此时 ws_->read() 抛 canceled 属于预期行为。
    // 若把它计为错误，连续几次中断就会耗尽 kMaxConsecutiveErrors，让本循环提前判死；
    // 之后即使上层重建连接，也再没有人读取，行情会永久停滞（本次线上故障根因）。
    if (m_stopped.load()) break;

    ++errors;
    LOG(ERROR) << fmt::format("{} Error in read_loop: {}", uri_, errmsg);
    if (errors < kMaxConsecutiveErrors) co_await backoff(errors);
  }
  if (errors >= kMaxConsecutiveErrors) {
    LOG(ERROR) << fmt::format("{} read_loop 连续错误 {} 次，停止读取", uri_, errors);
    // 必须关闭读取通道：否则上层 read() 会永久挂起在一个不再产生数据的通道上，
    // watch_public 的重连分支永远等不到返回，行情就此永久停滞——
    // 进程仍存活、守护脚本不会重启，线上曾因此停滞 42 小时无人察觉。
    // 关闭后 read() 带错返回，重连分支即可重建连接并重新订阅。
    m_stopped.store(true);
    read_channel_.close();
  }
}

asio::awaitable<void> OkxWs::write_loop() {
  int errors = 0;
  while (!m_stopped.load() && errors < kMaxConsecutiveErrors) {
    std::string errmsg;
    try {
      auto msg = co_await write_channel_.async_receive();
      co_await ws_->write(msg);
      errors = 0;
      continue;
    } catch (const boost::system::error_code& e) {
      errmsg = fmt::format("code {} {}", e.value(), e.what());
    } catch (const std::exception& e) {
      errmsg = e.what();
    } catch (...) {
      errmsg = "unknown error";
    }

    // 同 read_loop：主动中断导致的失败不计入连续错误。
    if (m_stopped.load()) break;

    ++errors;
    LOG(ERROR) << fmt::format("{} Error in write_loop: {}", uri_, errmsg);
    if (errors < kMaxConsecutiveErrors) co_await backoff(errors);
  }
  if (errors >= kMaxConsecutiveErrors) {
    LOG(ERROR) << fmt::format("{} write_loop 连续错误 {} 次，停止写入", uri_, errors);
    // 同 read_loop：写入通道也必须关闭，否则上层 write() 会永久挂起。
    m_stopped.store(true);
    write_channel_.close();
  }
}

std::string get_sign(std::string timestamp, std::string secret_key) {
  auto sign_str = timestamp + "GET" + "/users/self/verify";
  auto sign = Common::sha256_hash_base64(sign_str, secret_key);
  return sign;
}

}  // namespace market::okx
