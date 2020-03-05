// Context.hpp

#pragma once

#include <boost/asio/io_context.hpp>
#include <string>

namespace echo {
namespace asio = boost::asio;

class Context {
public:
  /**\brief you must call this function at first for initialize global context
   */
  static void init() noexcept;

  static asio::io_context &ioContext() noexcept;
};
} // namespace echo
