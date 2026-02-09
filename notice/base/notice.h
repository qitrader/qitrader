#ifndef QITRADER_NOTICE_BASE_NOTICE_H_
#define QITRADER_NOTICE_BASE_NOTICE_H_

#include "engine.h"
#include "utils/utils.h"

namespace notice::base {

class Notice : public std::enable_shared_from_this<Notice>, public engine::Component{
public:
  Notice(engine::EnginePtr engine);
  asio::awaitable<void> init() override;

  virtual asio::awaitable<void> send_message(engine::MessageDataPtr msg) = 0;
private:
  std::weak_ptr<engine::Engine> m_engine;  ///< 引擎弱引用，避免循环引用
};

}

#endif  // QITRADER_NOTICE_BASE_NOTICE_H_
