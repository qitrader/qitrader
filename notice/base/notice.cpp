#include "notice.h"
#include <glog/logging.h>

namespace notice::base {

Notice::Notice(engine::EnginePtr engine) : m_engine(engine) {}

asio::awaitable<void> Notice::init() {
  auto engine = m_engine.lock();
  if (!engine) {
    LOG(ERROR) << "Engine has been destroyed, cannot initialize notice";
    co_return;
  }
  
  engine->register_callback<engine::MessageData>(
    engine::EventType::kMessage, std::bind(&Notice::send_message, shared_from_this(), std::placeholders::_1));
  co_return;
}

}  // namespace notice::base
