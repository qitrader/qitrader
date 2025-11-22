#ifndef MARKET_OKX_OKX_HTTP_H_
#define MARKET_OKX_OKX_HTTP_H_

#include <memory>
#include <string>
#include "utils/utils.h"
#include <map>
#include "data.hpp"

namespace market::okx {

class OkxHttpRequest {
 public:
  OkxHttpRequest();

  asio::awaitable<std::string> request(const std::string& method, const std::string& request_path,
                      const std::string& body);
 private:
  std::map<std::string, std::string> get_headers(const std::string& method, const std::string& request_path,
                      const std::string& body);
  std::string api_key;
  std::string secret_key;
  std::string passphrase;
  const std::string baseUrl_ = "https://www.okx.com";
  bool sim = false;
};

typedef std::shared_ptr<OkxHttpRequest> OkxHttpRequestPtr;

class OkxHttp {
 public:
  OkxHttp();
  ~OkxHttp() {}

  asio::awaitable<Account> get_account();
  asio::awaitable<std::vector<PositionDetail>> get_positions();
  asio::awaitable<std::vector<QueryOrderDetail>> get_pending_orders();
  asio::awaitable<std::vector<SendOrderRspDetail>> send_orders(const std::vector<SendOrderRequest>& request);
  asio::awaitable<std::vector<CancelOrderRspDetail>> cancel_orders(const std::vector<CancelOrderRequest>& request);
 private:
  OkxHttpRequestPtr request_;
  
};

}  // namespace market::okx

#endif  // MARKET_OKX_OKX_HTTP_H_
