// Context.cpp

#include "echo/Context.hpp"
#include "logs.hpp"
#include <thread>

namespace echo {
static std::string sMongoUri_;

void Context::init() noexcept {
  // initialize asio io_context and mongo instance (they initialize at first
  // call)
  ioContext();
}

asio::io_context &Context::ioContext() noexcept {
  static asio::io_context context;
  return context;
}
} // namespace echo
