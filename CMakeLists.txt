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

set(WOLF_SV_TEST_INPUT_DIR "${CMAKE_SOURCE_DIR}/tests/data")
set(WOLF_SV_TEST_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/artifacts")

set(ELABORATE_INPUT_DIR "${WOLF_SV_TEST_INPUT_DIR}/elaborate")
set(ELABORATE_ARTIFACT_DIR "${WOLF_SV_TEST_ARTIFACT_DIR}/elaborate")

set(ELABORATE_SMOKE_INPUT "${ELABORATE_INPUT_DIR}/t0.sv")

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
        WOLF_SV_ELAB_DATA_DIR="${ELABORATE_INPUT_DIR}"
        WOLF_SV_ELAB_ARTIFACT_DIR="${ELABORATE_ARTIFACT_DIR}"
)

add_test(NAME elaborate-hierarchy COMMAND elaborate-hierarchy)

add_executable(elaborate-signal-memo
    tests/elaborate/test_elaborate_signal_memo.cpp
)

target_link_libraries(elaborate-signal-memo
    PRIVATE
        elaborate
)

target_compile_definitions(elaborate-signal-memo
    PRIVATE
        WOLF_SV_ELAB_SIGNAL_MEMO_PATH="${ELABORATE_INPUT_DIR}/signal_memo.sv"
)

add_test(NAME elaborate-signal-memo COMMAND elaborate-signal-memo)

add_executable(elaborate-rhs-converter
    tests/elaborate/test_elaborate_rhs_converter.cpp
)

target_link_libraries(elaborate-rhs-converter
    PRIVATE
        elaborate
)

target_compile_definitions(elaborate-rhs-converter
    PRIVATE
        WOLF_SV_ELAB_RHS_DATA_PATH="${ELABORATE_INPUT_DIR}/rhs_converter.sv"
        WOLF_SV_ELAB_RHS_ARTIFACT_PATH="${ELABORATE_ARTIFACT_DIR}/rhs_converter.json"
)

add_test(NAME elaborate-rhs-converter COMMAND elaborate-rhs-converter)

add_executable(elaborate-write-back-memo
    tests/elaborate/test_write_back_memo.cpp
)

target_link_libraries(elaborate-write-back-memo
    PRIVATE
        elaborate
)

add_test(NAME elaborate-write-back-memo COMMAND elaborate-write-back-memo)

# Installation rules
install(TARGETS wolf-sv-compiler DESTINATION bin)
