cmake_minimum_required(VERSION 3.16)

project(EdgeVolume LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core DBus)

add_executable(edge-volume
    src/main.cpp
)

target_link_libraries(edge-volume PRIVATE
    Qt6::Core
    Qt6::DBus
)

install(TARGETS edge-volume
    RUNTIME DESTINATION bin
)
