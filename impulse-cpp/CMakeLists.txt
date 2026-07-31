cmake_minimum_required(VERSION 3.20)
project(impulse_graph VERSION 2.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Shared Library (.dylib / .so / .dll)
add_library(impulse_graph SHARED src/impulse_graph.cpp)
target_compile_definitions(impulse_graph PRIVATE IMPULSE_BUILDING_DLL)
target_include_directories(impulse_graph PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Static Library (.a / .lib)
add_library(impulse_graph_static STATIC src/impulse_graph.cpp)
target_include_directories(impulse_graph_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

install(TARGETS impulse_graph impulse_graph_static DESTINATION lib)
install(FILES include/impulse_graph.h DESTINATION include)
