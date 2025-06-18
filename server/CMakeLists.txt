cmake_minimum_required(VERSION 3.10)
project(twamp-server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

find_package(Threads REQUIRED)

add_executable(twamp-server
    src/main.cpp
    src/Server.cpp
    src/Config.cpp
    src/Session.cpp
)

target_link_libraries(twamp-server PRIVATE Threads::Threads)

# Установка бинарника
install(TARGETS twamp-server DESTINATION /usr/local/bin)

# Установка конфига
install(FILES twamp-server.conf DESTINATION /usr/local/etc/twamp-server)

# Установка systemd unit
install(FILES twamp-server.service DESTINATION /etc/systemd/system)

# Post-install скрипт
install(CODE "
    execute_process(COMMAND mkdir -p /usr/local/etc/twamp-server)
    execute_process(COMMAND systemctl daemon-reload)
    execute_process(COMMAND systemctl enable twamp-server)
    " COMPONENT runtime)