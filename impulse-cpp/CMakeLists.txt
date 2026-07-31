cmake_minimum_required(VERSION 3.20)
project(impulse_graph VERSION 2.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_library(impulse_graph SHARED src/impulse_graph.cpp)

target_include_directories(impulse_graph PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

install(TARGETS impulse_graph DESTINATION lib)
install(FILES include/impulse_graph.h DESTINATION include)
