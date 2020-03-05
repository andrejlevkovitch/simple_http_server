// Session.cpp

#include "echo/Session.hpp"
#include "logs.hpp"
#include <boost/asio/coroutine.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/yield.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <regex>

#define SERVER_NAME "echo_server"

#define MIME_TEXT "text/plain"

namespace echo {
namespace beast = boost::beast;
namespace http  = beast::http;
using Request   = http::request<http::vector_body<uint8_t>>;
using Response  = http::response<http::vector_body<uint8_t>>;
using ArgMap    = std::multimap<std::string, std::string>;

/**\return return true and current path of target, if path is invalid return
 * false and empty string
 */
static std::pair<bool, std::string>
getPathFromTarget(const std::string &target) noexcept;

/**\return return true and ArgMap, if args is invalid return false and
 * empty ArgMap. Note: can return true and empty ArgMap - it means that args
 * section empty
 */
static std::pair<bool, ArgMap>
getArgsFromTarget(const std::string &target) noexcept;

class SessionImp : public asio::coroutine {
public:
  SessionImp(tcp::socket sock, CloseSignal &atClose) noexcept
      : sock_{std::move(sock)}
      , atClose_{atClose} {
  }

  /**\param self shared ptr to the SessionImp. We start some asynchronous
   * operations for the session, so if session will be close and some
   * asynchronous operation left - it cause error about using heap after free.
   * So the self shared pointer exists all time while asynchronos operations
   * (with the SessionImp) exists
   */
  void start(std::shared_ptr<SessionImp> self) noexcept {
    this->operator()(std::move(self), error_code{}, 0);
  }

  void close() noexcept {
    LOG_DEBUG("try close session");
    if (sock_.is_open()) {
      error_code error;

      sock_.cancel(error);
      if (error.failed()) {
        LOG_ERROR(error.message());
      }

      sock_.shutdown(tcp::socket::shutdown_both, error);
      if (error.failed()) {
        LOG_ERROR(error.message());
      }

      sock_.close(error);
      if (error.failed()) {
        LOG_ERROR(error.message());
      }
    }

    // emit signal about closing session
    atClose_();
  }

private:
  static void badRequest(Response &res, std::string_view message) {
    res.result(http::status::bad_request);
    res.set(http::field::content_type, MIME_TEXT);
    std::copy(message.begin(), message.end(), std::back_inserter(res.body()));
  }

  void handleRequest(const Request &req, Response &res) noexcept {
    // add info about server
    res.set(http::field::server, SERVER_NAME);

    // handle keep alive
    res.keep_alive(req.keep_alive());

    std::string target = req.target().to_string();
    if (const auto &[pathValid, path] = getPathFromTarget(target); pathValid) {
      LOG_DEBUG("path: %1%", path);

      std::string output = path;

      res.result(http::status::ok);
      res.set(http::field::content_type, MIME_TEXT);
      std::copy(output.begin(), output.end(), std::back_inserter(res.body()));
    } else { // path is invalid
      this->badRequest(res, "Invalid target");
    }

    // add Content-Length
    res.prepare_payload();
  }

  void operator()(std::shared_ptr<SessionImp> self,
                  const error_code &          error,
                  size_t                      transfered) noexcept {
    if (!error.failed()) {
      reenter(this) {
        for (;;) {
          yield http::async_read(sock_,
                                 buf_,
                                 req_,
                                 std::bind(&SessionImp::operator(),
                                           this,
                                           std::move(self),
                                           std::placeholders::_1,
                                           std::placeholders::_2));
          LOG_INFO("readed: %1%Kb", transfered / 1024.);

          LOG_DEBUG("%1% %2%", req_.method_string(), req_.target());

          this->handleRequest(req_, res_);

          yield http::async_write(sock_,
                                  res_,
                                  std::bind(&SessionImp::operator(),
                                            this,
                                            std::move(self),
                                            std::placeholders::_1,
                                            std::placeholders::_2));
          LOG_INFO("writed: %1%Kb", transfered / 1024.);

          if (res_.need_eof()) {
            LOG_DEBUG("need eof");
            close();

            // after closing we need stop coroutine
            yield break;
          }

          // cleare request and responce after processing
          req_.clear(); // clear fields
          req_.body().clear();

          res_.clear(); // clear body
          res_.body().clear();
        }
      }
    } else if (error == http::error::end_of_stream) {
      LOG_DEBUG("client close socket");
      close();
    } else {
      LOG_WARNING(error.message());
      close();
    }
  }

private:
  tcp::socket        sock_;
  beast::flat_buffer buf_;
  Request            req_;
  Response           res_;

  CloseSignal &atClose_;
};

Session::Session(tcp::socket sock) noexcept
    : imp_{std::make_shared<SessionImp>(std::move(sock), atClose)} {
  LOG_DEBUG("session opened");
}

Session::~Session() noexcept {
  LOG_DEBUG("session closed");
}

void Session::start() noexcept {
  LOG_DEBUG("start session");
  return imp_->start(imp_);
}

void Session::close() noexcept {
  return imp_->close();
}

static std::pair<bool, std::string>
getPathFromTarget(const std::string &target) noexcept {
  static const std::regex targetSplit{R"(\?)"};
  static const std::regex pathMatch{R"(^[\w/]+$)"};

  // XXX cicle have only one iteration!
  std::string path;
  for (std::sregex_token_iterator tokenIter{target.begin(),
                                            target.end(),
                                            targetSplit,
                                            -1};
       tokenIter != std::sregex_token_iterator{};
       ++tokenIter) {
    path = tokenIter->str();
    break;
  }

  std::smatch tmp;
  if (std::regex_match(path, tmp, pathMatch)) {
    return std::make_pair(true, path);
  } else {
    return std::make_pair(false, "");
  }
}

static std::pair<bool, ArgMap>
getArgsFromTarget(const std::string &target) noexcept {
  static const std::regex argsSplit{"&"};
  static const std::regex keyValSplit{"="};

  ulong pos = target.find("?");
  if (pos == std::string::npos) { // no args
    return std::make_pair(true, ArgMap{});
  }

  std::string argsStr =
      target.substr(pos + 1); // get substring after `?` symbol
  LOG_DEBUG("args: %1%", argsStr);

  ArgMap argMap;
  for (std::sregex_token_iterator argIter{argsStr.begin(),
                                          argsStr.end(),
                                          argsSplit,
                                          -1};
       argIter != std::sregex_token_iterator{};
       ++argIter) {
    std::string arg = argIter->str();

    // XXX list will have only two values: key in front, and value in back
    std::list<std::string> keyVal;
    for (std::sregex_token_iterator keyValIter{arg.begin(),
                                               arg.end(),
                                               keyValSplit,
                                               -1};
         keyValIter != std::sregex_token_iterator{};
         ++keyValIter) {
      keyVal.emplace_back(keyValIter->str());
    }

    if (keyVal.empty() || keyVal.size() > 2) { // args is invalid
      return std::make_pair(false, ArgMap{});
    }

    if (keyVal.size() ==
        1) { // in this case we have not value, so just set empty string
      keyVal.emplace_back("");
    }

    // XXX some of values or keys can be encoded, so we need decode it
    for (std::string &str : keyVal) {
      static const std::regex regex{R"(%\d+)"};

      std::string out;
      int         prev = 0; // previous position in str
      for (std::sregex_iterator i{str.begin(), str.end(), regex};
           i != std::sregex_iterator{};
           ++i) {
        out.append(str.substr(prev, i->position()));

        std::string encodedSymb = i->str();
        // special symbols encoded as `%F`
        int asciiNum = std::stoi(encodedSymb.substr(1), nullptr, 16);
        if (asciiNum != 0 && asciiNum < 255) {
          char ch[]{char(asciiNum), '\0'};
          out.append(ch);
        }

        // new position
        prev = i->position() + encodedSymb.size();
      }
      out.append(str.substr(prev)); // copy rest of symbols
    }

    argMap.emplace(keyVal.front(), keyVal.back());
  }

  return std::make_pair(true, std::move(argMap));
}
} // namespace echo
