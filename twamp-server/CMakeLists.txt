cmake_minimum_required(VERSION 3.10)
project(TWAMPServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(twamp_server twamp_server.cpp)

# Установка бинаря в /usr/local/bin
install(TARGETS twamp_server
    RUNTIME DESTINATION /usr/local/bin
)

# Установка systemd-сервиса в /etc/systemd/system
install(FILES twamp_server.service
    DESTINATION /etc/systemd/system
)

