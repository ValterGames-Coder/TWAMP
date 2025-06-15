cmake_minimum_required(VERSION 3.10)
project(twamp_server)

set(CMAKE_CXX_STANDARD 17)

add_executable(twamp_server twamp_server.cpp)

install(TARGETS twamp_server DESTINATION /usr/bin)
install(FILES twamp_server.service DESTINATION /etc/systemd/system)

