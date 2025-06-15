cmake_minimum_required(VERSION 3.10)
project(twamp_light_reflector)

set(CMAKE_CXX_STANDARD 17)

add_executable(twamp_light_reflector twamp_light_reflector.cpp)

install(TARGETS twamp_light_reflector DESTINATION /usr/local/bin)
install(FILES twamp_light_reflector.service DESTINATION /etc/systemd/system)

