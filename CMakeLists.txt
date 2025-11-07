cmake_minimum_required(VERSION 3.20)
project(wolf-sv-compiler VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(external/slang)

# GRH core library
add_library(grh STATIC
    src/grh.cpp
)

target_include_directories(grh
    PUBLIC
        include
)

target_link_libraries(grh
    PUBLIC
        slang::slang
)

# Application executable
add_executable(wolf-sv-compiler
    src/elaborate.cpp
    src/main.cpp
)

target_link_libraries(wolf-sv-compiler
    PRIVATE
        grh
)

enable_testing()

add_executable(grh-tests
    tests/grh/test_grh.cpp
)

target_link_libraries(grh-tests
    PRIVATE
        grh
)

target_compile_definitions(grh-tests
    PRIVATE
        GRH_STAGE1_JSON_PATH="${CMAKE_BINARY_DIR}/artifacts/grh_stage1_demo.json"
)

add_test(NAME grh-tests COMMAND grh-tests)

# Installation rules
install(TARGETS wolf-sv-compiler DESTINATION bin)
