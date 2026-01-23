cmake_minimum_required(VERSION 3.20)
project(wolf-sv-parser VERSION 0.1.0 LANGUAGES CXX)

# Toolchain / language
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Generate compile_commands.json for IDEs by default.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories (avoid per-target duplication)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Ensure important runtime/artifact directories exist at configure time
file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_subdirectory(external/slang)
add_subdirectory(src/pass)

# Libraries
## grh
add_library(grh STATIC
    src/grh.cpp
    src/load.cpp
    src/emit.cpp
)

target_include_directories(grh
    PUBLIC
        include
)

target_link_libraries(grh
    PUBLIC
        slang::slang
)

## transform
add_library(transform STATIC
    src/transform.cpp
    ${TRANSFORM_PASS_SOURCES}
)

target_include_directories(transform
    PUBLIC
        include
)

target_link_libraries(transform
    PUBLIC
        grh
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
add_executable(wolf-sv-parser
    src/main.cpp
)

target_link_libraries(wolf-sv-parser
    PRIVATE
        transform
        elaborate
        grh
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
set(EMIT_ARTIFACT_DIR "${WOLF_SV_TEST_ARTIFACT_DIR}/emit")

# Ensure artifact directories exist
file(MAKE_DIRECTORY "${WOLF_SV_TEST_ARTIFACT_DIR}")
file(MAKE_DIRECTORY "${ELABORATE_ARTIFACT_DIR}")
file(MAKE_DIRECTORY "${EMIT_ARTIFACT_DIR}")

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

# grh roundtrip tests
add_executable(grh-roundtrip-tests
    tests/grh/test_grh_roundtrip.cpp
)

target_link_libraries(grh-roundtrip-tests
    PRIVATE
        grh
)

register_test_exe(grh-roundtrip-tests)

# emit base
add_executable(emit-base
    tests/emit/test_emit_base.cpp
)

target_link_libraries(emit-base
    PRIVATE
        grh
)

target_compile_definitions(emit-base
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
)

register_test_exe(emit-base)

add_executable(emit-json
    tests/emit/test_emit_json.cpp
)

target_link_libraries(emit-json
    PRIVATE
        grh
)

target_compile_definitions(emit-json
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
)

register_test_exe(emit-json)

add_executable(emit-json-ir
    tests/emit/test_emit_json_ir.cpp
)

target_link_libraries(emit-json-ir
    PRIVATE
        grh
)

target_compile_definitions(emit-json-ir
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
)

register_test_exe(emit-json-ir)

add_executable(emit-sv
    tests/emit/test_emit_sv.cpp
)

target_link_libraries(emit-sv
    PRIVATE
        grh
        elaborate
)

target_compile_definitions(emit-sv
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
        WOLF_SV_EMIT_SV_INPUT_PATH="${WOLF_SV_TEST_INPUT_DIR}/emit/emit_sv_input.sv"
)

register_test_exe(emit-sv)

add_executable(emit-sv-ir
    tests/emit/test_emit_sv_ir.cpp
)

target_link_libraries(emit-sv-ir
    PRIVATE
        grh
)

target_compile_definitions(emit-sv-ir
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
)

register_test_exe(emit-sv-ir)

# elaborate: smoke
add_executable(elaborate-smoke
    tests/elaborate/test_elaborate_smoke.cpp
)

target_link_libraries(elaborate-smoke
    PRIVATE
        elaborate
        grh
)

target_compile_definitions(elaborate-smoke
    PRIVATE
        WOLF_SV_ELAB_SMOKE_INPUT_PATH="${ELABORATE_SMOKE_INPUT}"
)

register_test_exe(elaborate-smoke)

# transform tests
add_executable(transform-pass-manager
    tests/transform/test_transform_pass_manager.cpp
)

target_link_libraries(transform-pass-manager
    PRIVATE
        transform
)

register_test_exe(transform-pass-manager)

add_executable(transform-grh-verify
    tests/transform/test_grh_verify_pass.cpp
)

target_link_libraries(transform-grh-verify
    PRIVATE
        transform
)

register_test_exe(transform-grh-verify)

add_executable(transform-const-fold
    tests/transform/test_const_fold_pass.cpp
)

target_link_libraries(transform-const-fold
    PRIVATE
        transform
)

register_test_exe(transform-const-fold)

# elaborate: hierarchy
add_executable(elaborate-hierarchy
    tests/elaborate/test_elaborate_hierarchy.cpp
)

target_link_libraries(elaborate-hierarchy
    PRIVATE
        elaborate
        grh
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
        grh
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
        grh
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
        grh
)

register_test_exe(elaborate-write-back-memo)

# elaborate: assign
add_executable(elaborate-assign
    tests/elaborate/test_elaborate_assign.cpp
)

target_link_libraries(elaborate-assign
    PRIVATE
        elaborate
        grh
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
        grh
)

target_compile_definitions(elaborate-comb-always
    PRIVATE
        WOLF_SV_ELAB_COMB_ALWAYS_DATA_PATH="${ELABORATE_INPUT_DIR}/comb_always.sv"
        WOLF_SV_ELAB_COMB_ALWAYS_ARTIFACT_PATH="${ELABORATE_ARTIFACT_DIR}/comb_always.json"
)

register_test_exe(elaborate-comb-always)

add_executable(elaborate-latch
    tests/elaborate/test_elaborate_latch.cpp
)

target_link_libraries(elaborate-latch
    PRIVATE
        elaborate
        grh
)

target_compile_definitions(elaborate-latch
    PRIVATE
        WOLF_SV_ELAB_LATCH_DATA_PATH="${ELABORATE_INPUT_DIR}/latch.sv"
        WOLF_SV_ELAB_LATCH_ARTIFACT_PATH="${ELABORATE_ARTIFACT_DIR}/latch.json"
)

register_test_exe(elaborate-latch)

# elaborate: seq always
add_executable(elaborate-seq-always
    tests/elaborate/test_elaborate_seq_always.cpp
)

target_link_libraries(elaborate-seq-always
    PRIVATE
        elaborate
        grh
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
        grh
)

target_compile_definitions(elaborate-stage21
    PRIVATE
        WOLF_SV_ELAB_SEQ_ALWAYS_DATA_PATH="${ELABORATE_INPUT_DIR}/seq_always.sv"
)

register_test_exe(elaborate-stage21)

# elaborate: blackbox
add_executable(elaborate-blackbox
    tests/elaborate/test_elaborate_blackbox.cpp
)

target_link_libraries(elaborate-blackbox
    PRIVATE
        elaborate
        grh
)

target_compile_definitions(elaborate-blackbox
    PRIVATE
        WOLF_SV_ELAB_BLACKBOX_DATA_PATH="${ELABORATE_INPUT_DIR}/blackbox.sv"
)

register_test_exe(elaborate-blackbox)

# elaborate: inout
add_executable(elaborate-inout
    tests/elaborate/test_elaborate_inout.cpp
)

target_link_libraries(elaborate-inout
    PRIVATE
        elaborate
        grh
)

target_compile_definitions(elaborate-inout
    PRIVATE
        WOLF_SV_ELAB_INOUT_DATA_PATH="${ELABORATE_INPUT_DIR}/inout.sv"
)

register_test_exe(elaborate-inout)

# elaborate: dpic
add_executable(elaborate-dpic
    tests/elaborate/test_elaborate_dpic.cpp
)

target_link_libraries(elaborate-dpic
    PRIVATE
        elaborate
        grh
)

target_compile_definitions(elaborate-dpic
    PRIVATE
        WOLF_SV_ELAB_DPIC_DATA_PATH="${ELABORATE_INPUT_DIR}/dpic.sv"
)

register_test_exe(elaborate-dpic)

# Installation rules
install(TARGETS wolf-sv-parser DESTINATION bin)
