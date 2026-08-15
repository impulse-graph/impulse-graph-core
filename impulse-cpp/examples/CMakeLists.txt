cmake_minimum_required(VERSION 3.20)
project(impulse_cpp_examples LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Common Include Dirs ---
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

# --- Example 01: Snapshot Basics ---
add_executable(01_snapshot_basics 01_snapshot_basics/main.cpp)
target_link_libraries(01_snapshot_basics PRIVATE impulse_graph_static)

# --- Example 02: Social Graph Traversal ---
add_executable(02_social_traversal 02_social_traversal/main.cpp)
target_link_libraries(02_social_traversal PRIVATE impulse_graph_static)

# --- Example 03: ReBAC Reachability ---
add_executable(03_rbac_reachability 03_rbac_reachability/main.cpp)
target_link_libraries(03_rbac_reachability PRIVATE impulse_graph_static)

# --- Example 04: Google CEL Transactions ---
add_executable(04_cel_transactions 04_cel_transactions/main.cpp)
target_link_libraries(04_cel_transactions PRIVATE impulse_graph_static)
