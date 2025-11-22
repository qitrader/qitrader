#include "okx_http.h"
#include "utils/utils.h"
#include <map>
#include "httpcpp/request.h"
#include "jsoncpp/jsoncpp.hpp"
#include <glog/logging.h>

namespace market::okx {

OkxHttpRequest::OkxHttpRequest()
  : api_key(okx_config->api_key()), secret_key(okx_config->secret_key()), passphrase(okx_config->passphrase()), sim(okx_config->sim()) {}

asio::awaitable<std::string> OkxHttpRequest::request(
  const std::string& method, const std::string& request_path, const std::string& body){

  cpphttp::HttpRequest request(baseUrl_ + request_path, method, body);
  auto headers = get_headers(method, request_path, body);
  request.set_header(headers);
  if (method == "POST") {
    request.set_body("application/json", body);
  }
  
  auto resp = co_await request.request();

  co_return resp;
}

std::map<std::string, std::string> OkxHttpRequest::get_headers(
  const std::string& method, const std::string& request_path, const std::string& body){
  auto ts = Common::get_current_time_s();
  std::string str_to_sign = fmt::format("{}{}{}{}", Common::time_format_iso(ts), method, request_path, body);
  std::string hash = Common::sha256_hash_base64(str_to_sign, secret_key);
  std::map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  headers["OK-ACCESS-KEY"] = api_key;
  headers["OK-ACCESS-SIGN"] = hash;
  headers["OK-ACCESS-TIMESTAMP"] = Common::time_format_iso(ts);
  headers["OK-ACCESS-PASSPHRASE"] = passphrase;
  if (sim) {
    headers["x-simulated-trading"] = "1";
  }
  return headers;
}

OkxHttp::OkxHttp()
  : request_(std::make_shared<OkxHttpRequest>())
{
  
}

asio::awaitable<Account> OkxHttp::get_account(){
  auto resp = co_await request_->request("GET", "/api/v5/account/balance", "");

  auto account_rsp = jsoncpp::from_json<AccountRespone>(resp);

  if (account_rsp->code != 0) {
    LOG(ERROR) << "get account failed, code: " << account_rsp->code << ", msg: " << account_rsp->msg;
    throw std::runtime_error(fmt::format("get account failed, code: {}, msg: {}", account_rsp->code, account_rsp->msg));
  }

  if (account_rsp->data.size() == 0) {
    LOG(ERROR) << "get account failed, no data returned";
    throw std::runtime_error("get account failed, no data returned");
  }

  co_return account_rsp->data[0];
}

asio::awaitable<std::vector<PositionDetail>> OkxHttp::get_positions(){
  auto resp = co_await request_->request("GET", "/api/v5/account/positions", "");
  auto position_rsp = jsoncpp::from_json<PositionRespone>(resp);
  if (position_rsp->code != 0) {
    LOG(ERROR) << "get positions failed, code: " << position_rsp->code << ", msg: " << position_rsp->msg;
    throw std::runtime_error(fmt::format("get positions failed, code: {}, msg: {}", position_rsp->code, position_rsp->msg));
  }

  co_return position_rsp->data;
}

asio::awaitable<std::vector<QueryOrderDetail>> OkxHttp::get_orders(){
  auto resp = co_await request_->request("GET", "/api/v5/trade/orders-pending", "");
  LOG(INFO) << "get orders response: " << resp;
  auto order_rsp = jsoncpp::from_json<QueryOrderRespone>(resp);
  if (order_rsp->code != 0) {
    LOG(ERROR) << "get orders failed, code: " << order_rsp->code << ", msg: " << order_rsp->msg;
    throw std::runtime_error(fmt::format("get orders failed, code: {}, msg: {}", order_rsp->code, order_rsp->msg));
  }

  co_return order_rsp->data;
}

asio::awaitable<std::vector<SendOrderRspDetail>> OkxHttp::send_orders(const std::vector<SendOrderRequest>& request){
  LOG(INFO) << "send orders: " << jsoncpp::to_json(request);
  auto resp = co_await request_->request(
    "POST", "/api/v5/trade/batch-orders", jsoncpp::to_json(request));
  auto order_rsp = jsoncpp::from_json<SendOrderRespone>(resp);
  if (order_rsp->code != 0 && order_rsp->code != 1 && order_rsp->code != 2) {
    LOG(ERROR) << "send order failed, code: " << order_rsp->code << ", msg: " << order_rsp->msg;
    throw std::runtime_error(fmt::format("send order failed, code: {}, msg: {}", order_rsp->code, order_rsp->msg));
  }

  co_return order_rsp->data;
}

}
