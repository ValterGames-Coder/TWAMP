cmake_minimum_required(VERSION 3.10)
project(twamp-client)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Указываем включаемые директории
include_directories(include)

add_executable(twamp-client
    src/main.cpp
    src/Client.cpp
    src/ClientSession.cpp
)

# Установка в /usr/bin
install(TARGETS twamp-client DESTINATION /usr/bin)