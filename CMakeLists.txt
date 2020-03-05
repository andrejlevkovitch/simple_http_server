# cmake

cmake_minimum_required(VERSION 3.12)

project(echo_server)

include(cmake/build.cmake)
include(cmake/doxygen.cmake)

find_package(Boost COMPONENTS program_options REQUIRED)
find_package(Threads REQUIRED)

set(PROJECT_SRC
  src/echo/Context.cpp
  src/echo/Server.cpp
  src/echo/Session.cpp
  src/echo/SessionPool.cpp
  src/main.cpp
  )

add_executable(${PROJECT_NAME} ${PROJECT_SRC})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE
  Boost::boost
  Threads::Threads
  ${Boost_LIBRARIES}
  )
target_include_directories(${PROJECT_NAME} PRIVATE
  include
  )

# needed for colorize logs int console
if(COLORIZED)
  target_compile_definitions(${PROJECT_NAME} PRIVATE -DCOLORIZED)
endif()
