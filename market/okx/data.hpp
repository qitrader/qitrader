#ifndef MARKET_OKX_DATA_H_
#define MARKET_OKX_DATA_H_

#include <utils/utils.h>

#include <string>
#include <vector>

#include "config/config.h"
namespace market::okx {

class OkxConfig : public Config::ConfigTree {
 public:
  OkxConfig() : ConfigTree("okx"){};

  void load(std::shared_ptr<Config::ptree> pt) override {
    m_ptree = pt;

    m_api_key = this->get<std::string>("api_key");
    m_secret_key = this->get<std::string>("secret_key");
    m_passphrase = this->get<std::string>("passphrase");
    m_sim = this->get<bool>("sim");
  }

  std::string api_key() const { return m_api_key; }
  std::string secret_key() const { return m_secret_key; }
  std::string passphrase() const { return m_passphrase; }
  bool sim() const { return m_sim; }

 private:
  std::string m_api_key;
  std::string m_secret_key;
  std::string m_passphrase;
  bool m_sim;
};

#define okx_config ::Common::SingletonPtr<::market::okx::OkxConfig>::get_instance()

template <typename T>
struct Respone {
  int code;
  std::string msg;
  T data;
};

struct AccountDetail {
  uint64_t uTime;
  std::string ccy;
  dec_float eq;
  dec_float cashBal;
  dec_float availBal;
};

struct Account {
  uint64_t uTime;
  dec_float totalEq;
  std::vector<AccountDetail> details;
};

typedef Respone<std::vector<Account>> AccountRespone;

struct PositionDetail {
  uint64_t uTime;
  std::string instType;
  std::string posId;
  std::string ccy;
  std::string posSide;

  dec_float pos;
  dec_float avgPx;
  dec_float pnl;
};

typedef Respone<std::vector<PositionDetail>> PositionRespone;

struct QueryOrderDetail {
  uint64_t uTime;
  std::string instId;
  std::string ordId;

  dec_float px;         // 委托价格
  dec_float sz;         // 委托数量
  std::string side;     // 委托方向
  dec_float accFillSz;  // 已成交数量
  dec_float avgPx;      // 平均成交价格
  std::string state;    // 订单状态
};

typedef Respone<std::vector<QueryOrderDetail>> QueryOrderRespone;

struct SendOrderRequest {
  std::string instId;
  std::string tdMode;
  std::string ccy;
  std::string clOrdId;
  std::string tag;
  std::string side;
  std::string posSide;
  std::string ordType;
  std::string tgtCcy;
  
  dec_float sz;
  dec_float px;
};

struct SendOrderRspDetail {
  std::string instId;
  std::string ordId;
  std::string clOrdId;
  std::string tag;
  int64_t ts;
  int sCode;
  std::string sMsg;
};

typedef Respone<std::vector<SendOrderRspDetail>> SendOrderRespone;


struct CancelOrderRequest {
  std::string instId;
  std::string ordId;
  std::string clOrdId;
};

struct CancelOrderRspDetail {
  std::string ordId;
  std::string clOrdId;
  int64_t ts; // 毫秒
  int sCode;
  std::string sMsg;
};

typedef Respone<std::vector<CancelOrderRspDetail>> CancelOrderRespone;

template <typename T>
struct WsRequest {
  std::string op;
  T args;
};

struct WsSubscibeDetail {
  std::string channel;
  std::string instId;
};

typedef WsRequest<std::vector<WsSubscibeDetail>> WsSubscibeRequest;

struct WsLoginDetail {
  std::string apiKey;
  std::string passphrase;
  std::string timestamp;
  std::string sign;
};

typedef WsRequest<std::vector<WsLoginDetail>> WsLoginRequest;

struct WsSubscibeAccountDetail {
  std::string channel;
  std::string ccy;
};

typedef WsRequest<std::vector<WsSubscibeAccountDetail>> WsSubscibeAccountRequest;

struct WsSubscibePositionDetail {
  std::string channel;
  std::string instType;
};

typedef WsRequest<std::vector<WsSubscibePositionDetail>> WsSubscibePositionRequest;

struct WsSubscibeOrderDetail {
  std::string channel;
  std::string instType;
};

typedef WsRequest<std::vector<WsSubscibeOrderDetail>> WsSubscibeOrderRequest;

struct WsArg {
  std::string channel;
  std::string instId;
};

struct WsMessage {
  std::string event;
  std::string connId;
  WsArg arg;
  
  // 数据
  std::any data;
  std::string action;

  // 错误
  int64_t code;
  std::string msg;

  int connCount;
};

struct WsTick {
  std::string instId;
  std::string instType;
  dec_float last;
  dec_float lastSz;

  dec_float bidPx;
  dec_float bidSz;
  dec_float askPx;
  dec_float askSz;

  dec_float open24h;
  dec_float high24h;
  dec_float low24h;
  dec_float volCcy24h;
  dec_float vol24h;
  dec_float sodUtc0;
  dec_float sodUtc8;
  
  int64_t ts;
};

struct WsBookItem {
  dec_float price;
  dec_float size;
  int order_num;
};

struct WsBook {
  std::vector<WsBookItem> bids;
  std::vector<WsBookItem> asks;

  uint64_t ts;
  int64_t check_sum;
  int64_t prevSeqId;
  int64_t seqId;
};

}  // namespace market::okx

namespace jsoncpp {

template <>
struct transform<market::okx::WsBookItem> {
  static void trans(const bj::value &jv, market::okx::WsBookItem &t) {
    auto ja = jv.as_array();
    t.price = dec_float(ja.at(0).as_string().c_str());
    t.size = dec_float(ja.at(1).as_string().c_str());
    t.order_num = std::stoll(ja.at(2).as_string().c_str());
  }
};

template <>
struct transform<market::okx::WsMessage> {
  static void trans(const bj::value &jv, market::okx::WsMessage &t) {
    auto jo = jv.as_object();
    if (jo.contains("event")) {
      t.event = jo.at("event").as_string();
    }

    if (jo.contains("connId")) {
      t.connId = jo.at("connId").as_string();
    }

    if (jo.contains("arg")) {
      transform<market::okx::WsArg>::trans(jo.at("arg"), t.arg);
    }

    if (!t.event.empty()) {  // 如果没有event，就是数据
      trans_event(jo, t);
      return;
    }

    trans_data(jo, t);
    return;
  }

  static void trans_event(const bj::object &jo, market::okx::WsMessage &t) {
    if (t.event == "error") {
      t.msg = jo.at("msg").as_string();
      transform<decltype(t.code)>::trans(jo.at("code"), t.code);
    } else if (t.event == "channel-conn-count") {
      t.connCount = jo.at("connCount").as_int64();
    }
  }

  static void trans_data(const bj::object &jo, market::okx::WsMessage &t) {
    if (t.arg.channel == "tickers") {
      auto data = std::vector<market::okx::WsTick>();
      transform<decltype(data)>::trans(jo.at("data"), data);
      t.data = data;
    } else if (t.arg.channel == "books") {
      auto data = std::vector<market::okx::WsBook>();
      transform<decltype(data)>::trans(jo.at("data"), data);
      t.data = data;
      
      t.action = jo.at("action").as_string();
    } else if (t.arg.channel == "account") {
      auto data = std::vector<market::okx::Account>();
      transform<decltype(data)>::trans(jo.at("data"), data);
      t.data = data;
    } else if (t.arg.channel == "positions") {
      auto data = std::vector<market::okx::PositionDetail>();
      transform<decltype(data)>::trans(jo.at("data"), data);
      t.data = data;
    } else {
      throw std::runtime_error("invalid channel");
    }
  }
};
}  // namespace jsoncpp

#endif  // MARKET_OKX_DATA_H_
