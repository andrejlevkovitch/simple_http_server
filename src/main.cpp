// main.cpp

#include "echo/Server.hpp"
#include "logs.hpp"
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <cstdlib>

#define IP_ARG         "ip"
#define PORT_ARG       "port"
#define LIMIT_SESSIONS "lim_conn"

#define DEFAULT_IP             "localhost"
#define DEFAULT_PORT           9173
#define DEFAULT_LIMIT_SESSIONS 0

int main(int argc, char *argv[]) {
  namespace po = boost::program_options;

  // parse args
  po::options_description options;

  // add options for server
  options.add_options()(
      IP_ARG,
      po::value<std::string>()->required()->default_value(DEFAULT_IP),
      "ip for server")(
      PORT_ARG,
      po::value<uint>()->required()->default_value(DEFAULT_PORT),
      "port for server")(
      LIMIT_SESSIONS,
      po::value<uint>()->required()->default_value(DEFAULT_LIMIT_SESSIONS),
      "maximum capacity of opened connections, 0 is special value, which means "
      "that capacity of sessions not limited");

  po::variables_map argMap;
  po::store(po::parse_command_line(argc, argv, options), argMap);
  po::notify(argMap);

  std::string ip               = argMap[IP_ARG].as<std::string>();
  uint        port             = argMap[PORT_ARG].as<uint>();
  uint        maxSessionsCount = argMap[LIMIT_SESSIONS].as<uint>();

  echo::Server server{maxSessionsCount};
  server.run(ip, port);

  LOG_DEBUG("exit");

  return EXIT_SUCCESS;
}
