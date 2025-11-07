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

add_library(elaborate STATIC
    src/elaborate.cpp
)

target_include_directories(elaborate
    PUBLIC
        include
)

target_link_libraries(elaborate
    PUBLIC
        grh
)

# Application executable
add_executable(wolf-sv-compiler
    src/main.cpp
)

target_link_libraries(wolf-sv-compiler
    PRIVATE
        elaborate
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

add_executable(elaborate-smoke
    tests/elaborate/test_elaborate_smoke.cpp
)

target_link_libraries(elaborate-smoke
    PRIVATE
        elaborate
)

set(ELABORATE_SMOKE_INPUT "${CMAKE_SOURCE_DIR}/tests/elaborate/data/t0.sv")

target_compile_definitions(elaborate-smoke
    PRIVATE
        WOLF_SV_ELAB_SMOKE_INPUT_PATH="${ELABORATE_SMOKE_INPUT}"
)

add_test(NAME elaborate-smoke COMMAND elaborate-smoke)

add_executable(elaborate-hierarchy
    tests/elaborate/test_elaborate_hierarchy.cpp
)

target_link_libraries(elaborate-hierarchy
    PRIVATE
        elaborate
)

target_compile_definitions(elaborate-hierarchy
    PRIVATE
        WOLF_SV_ELAB_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/elaborate/data"
        WOLF_SV_ELAB_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/artifacts/elaborate_stage3"
)

add_test(NAME elaborate-hierarchy COMMAND elaborate-hierarchy)

# Installation rules
install(TARGETS wolf-sv-compiler DESTINATION bin)
