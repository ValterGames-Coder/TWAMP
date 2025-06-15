cmake_minimum_required(VERSION 3.10)
project(twamp-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR}/include)

add_executable(twamp-server
    src/main.cpp
    src/Daemonize.cpp
    src/TwampControlServer.cpp
    src/SessionReflector.cpp
)

target_link_libraries(twamp-server
    pthread
)

install(TARGETS twamp-server DESTINATION /usr/bin)
install(FILES service/twamp-server.service DESTINATION /etc/systemd/system)