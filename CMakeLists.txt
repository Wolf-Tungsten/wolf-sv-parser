cmake_minimum_required(VERSION 3.20)
project(wolf-sv-compiler VERSION 0.1.0 LANGUAGES CXX)

# Toolchain / language
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories (avoid per-target duplication)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Ensure important runtime/artifact directories exist at configure time
file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_subdirectory(external/slang)

# Libraries
## grh
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

## elaborate
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

# Application
add_executable(wolf-sv-compiler
    src/main.cpp
)

target_link_libraries(wolf-sv-compiler
    PRIVATE
        elaborate
)

enable_testing()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
# Common test paths
set(WOLF_SV_TEST_INPUT_DIR "${CMAKE_SOURCE_DIR}/tests/data")
set(WOLF_SV_TEST_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/artifacts")

set(ELABORATE_INPUT_DIR "${WOLF_SV_TEST_INPUT_DIR}/elaborate")
set(ELABORATE_ARTIFACT_DIR "${WOLF_SV_TEST_ARTIFACT_DIR}/elaborate")
set(ELABORATE_SMOKE_INPUT "${ELABORATE_INPUT_DIR}/t0.sv")

# Ensure artifact directories exist
file(MAKE_DIRECTORY "${WOLF_SV_TEST_ARTIFACT_DIR}")
file(MAKE_DIRECTORY "${ELABORATE_ARTIFACT_DIR}")

# Helper: register an executable as a ctest by its target file (no hardcoded paths)
function(register_test_exe target_name)
    add_test(NAME "${target_name}" COMMAND $<TARGET_FILE:${target_name}>)
endfunction()

# grh tests
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

register_test_exe(grh-tests)

# elaborate: smoke
add_executable(elaborate-smoke
    tests/elaborate/test_elaborate_smoke.cpp
)

target_link_libraries(elaborate-smoke
    PRIVATE
        elaborate
)

target_compile_definitions(elaborate-smoke
    PRIVATE
        WOLF_SV_ELAB_SMOKE_INPUT_PATH="${ELABORATE_SMOKE_INPUT}"
)

register_test_exe(elaborate-smoke)

# elaborate: hierarchy
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

register_test_exe(elaborate-hierarchy)

# elaborate: signal memo
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

register_test_exe(elaborate-signal-memo)

# elaborate: rhs converter
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

register_test_exe(elaborate-rhs-converter)

# elaborate: write back memo
add_executable(elaborate-write-back-memo
    tests/elaborate/test_write_back_memo.cpp
)

target_link_libraries(elaborate-write-back-memo
    PRIVATE
        elaborate
)

register_test_exe(elaborate-write-back-memo)

# elaborate: assign
add_executable(elaborate-assign
    tests/elaborate/test_elaborate_assign.cpp
)

target_link_libraries(elaborate-assign
    PRIVATE
        elaborate
)

target_compile_definitions(elaborate-assign
    PRIVATE
        WOLF_SV_ELAB_ASSIGN_DATA_PATH="${ELABORATE_INPUT_DIR}/continuous_assign.sv"
        WOLF_SV_ELAB_ASSIGN_ARTIFACT_PATH="${ELABORATE_ARTIFACT_DIR}/assign_stage11.json"
)

register_test_exe(elaborate-assign)

# elaborate: comb always
add_executable(elaborate-comb-always
    tests/elaborate/test_elaborate_comb_always.cpp
)

target_link_libraries(elaborate-comb-always
    PRIVATE
        elaborate
)

target_compile_definitions(elaborate-comb-always
    PRIVATE
        WOLF_SV_ELAB_COMB_ALWAYS_DATA_PATH="${ELABORATE_INPUT_DIR}/comb_always.sv"
        WOLF_SV_ELAB_COMB_ALWAYS_ARTIFACT_PATH="${ELABORATE_ARTIFACT_DIR}/comb_always.json"
)

register_test_exe(elaborate-comb-always)

# elaborate: seq always
add_executable(elaborate-seq-always
    tests/elaborate/test_elaborate_seq_always.cpp
)

target_link_libraries(elaborate-seq-always
    PRIVATE
        elaborate
)

target_compile_definitions(elaborate-seq-always
    PRIVATE
        WOLF_SV_ELAB_SEQ_ALWAYS_DATA_PATH="${ELABORATE_INPUT_DIR}/seq_always.sv"
        WOLF_SV_ELAB_SEQ_ALWAYS_ARTIFACT_PATH="${ELABORATE_ARTIFACT_DIR}/seq_always.json"
)

register_test_exe(elaborate-seq-always)

# elaborate: stage21
add_executable(elaborate-stage21
    tests/elaborate/test_elaborate_stage21.cpp
)

target_link_libraries(elaborate-stage21
    PRIVATE
        elaborate
)

target_compile_definitions(elaborate-stage21
    PRIVATE
        WOLF_SV_ELAB_SEQ_ALWAYS_DATA_PATH="${ELABORATE_INPUT_DIR}/seq_always.sv"
)

register_test_exe(elaborate-stage21)

# elaborate: dpic
add_executable(elaborate-dpic
    tests/elaborate/test_elaborate_dpic.cpp
)

target_link_libraries(elaborate-dpic
    PRIVATE
        elaborate
)

target_compile_definitions(elaborate-dpic
    PRIVATE
        WOLF_SV_ELAB_DPIC_DATA_PATH="${ELABORATE_INPUT_DIR}/dpic.sv"
)

register_test_exe(elaborate-dpic)

# Installation rules
install(TARGETS wolf-sv-compiler DESTINATION bin)
