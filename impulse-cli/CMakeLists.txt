cmake_minimum_required(VERSION 3.20)
project(impulse_tools LANGUAGES NONE)

find_program(CARGO_EXECUTABLE cargo REQUIRED)

add_custom_target(impulse-graph ALL
    COMMAND ${CARGO_EXECUTABLE} build --release
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building Impulse Graph unified Rust tooling binary (impulse-graph)"
)
