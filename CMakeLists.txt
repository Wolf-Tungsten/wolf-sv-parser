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

## convert
add_library(convert STATIC
    src/convert.cpp
)

target_include_directories(convert
    PUBLIC
        include
)

target_link_libraries(convert
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
        convert
        grh
)

enable_testing()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
# Common test paths
set(WOLF_SV_TEST_INPUT_DIR "${CMAKE_SOURCE_DIR}/tests/data")
set(WOLF_SV_TEST_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/artifacts")

set(EMIT_ARTIFACT_DIR "${WOLF_SV_TEST_ARTIFACT_DIR}/emit")

# Ensure artifact directories exist
file(MAKE_DIRECTORY "${WOLF_SV_TEST_ARTIFACT_DIR}")
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

# transform tests
add_executable(transform-pass-manager
    tests/transform/test_transform_pass_manager.cpp
)

target_link_libraries(transform-pass-manager
    PRIVATE
        transform
)

register_test_exe(transform-pass-manager)

add_executable(transform-const-fold
    tests/transform/test_const_fold_pass.cpp
)

target_link_libraries(transform-const-fold
    PRIVATE
        transform
)

register_test_exe(transform-const-fold)

# convert: symbol collector
add_executable(convert-symbol-collector
    tests/convert/test_convert_symbol_collector.cpp
)

target_link_libraries(convert-symbol-collector
    PRIVATE
        convert
        grh
)

target_compile_definitions(convert-symbol-collector
    PRIVATE
        WOLF_SV_CONVERT_HDLBITS_DUT_DIR="${WOLF_SV_TEST_INPUT_DIR}/hdlbits/dut"
)

register_test_exe(convert-symbol-collector)

# convert: type resolver
add_executable(convert-type-resolver
    tests/convert/test_convert_type_resolver.cpp
)

target_link_libraries(convert-type-resolver
    PRIVATE
        convert
        grh
)

target_compile_definitions(convert-type-resolver
    PRIVATE
        WOLF_SV_CONVERT_HDLBITS_DUT_DIR="${WOLF_SV_TEST_INPUT_DIR}/hdlbits/dut"
)

register_test_exe(convert-type-resolver)

# convert: rw analyzer
add_executable(convert-rw-analyzer
    tests/convert/test_convert_rw_analyzer.cpp
)

target_link_libraries(convert-rw-analyzer
    PRIVATE
        convert
        grh
)

target_compile_definitions(convert-rw-analyzer
    PRIVATE
        WOLF_SV_CONVERT_HDLBITS_DUT_DIR="${WOLF_SV_TEST_INPUT_DIR}/hdlbits/dut"
)

register_test_exe(convert-rw-analyzer)

# convert: expr lowerer
add_executable(convert-expr-lowerer
    tests/convert/test_convert_expr_lowerer.cpp
)

target_link_libraries(convert-expr-lowerer
    PRIVATE
        convert
        grh
)

target_compile_definitions(convert-expr-lowerer
    PRIVATE
        WOLF_SV_CONVERT_EXPR_DATA_PATH="${WOLF_SV_TEST_INPUT_DIR}/convert/expr_lowerer.sv"
)

register_test_exe(convert-expr-lowerer)
# convert: stmt lowerer
add_executable(convert-stmt-lowerer
    tests/convert/test_convert_stmt_lowerer.cpp
)
target_link_libraries(convert-stmt-lowerer
    PRIVATE
        convert
)
target_compile_definitions(convert-stmt-lowerer
    PRIVATE
        WOLF_SV_CONVERT_STMT_DATA_PATH="${WOLF_SV_TEST_INPUT_DIR}/convert/stmt_lowerer.sv"
)
register_test_exe(convert-stmt-lowerer)

# convert: stmt lowerer error paths
add_executable(convert-stmt-lowerer-errors
    tests/convert/test_convert_stmt_lowerer_errors.cpp
)
target_link_libraries(convert-stmt-lowerer-errors
    PRIVATE
        convert
)
target_compile_definitions(convert-stmt-lowerer-errors
    PRIVATE
        WOLF_SV_CONVERT_STMT_ERROR_DATA_PATH="${WOLF_SV_TEST_INPUT_DIR}/convert/stmt_lowerer_errors.sv"
)
register_test_exe(convert-stmt-lowerer-errors)

# convert: write-back
add_executable(convert-write-back
    tests/convert/test_convert_write_back.cpp
)
target_link_libraries(convert-write-back
    PRIVATE
        convert
)
target_compile_definitions(convert-write-back
    PRIVATE
        WOLF_SV_CONVERT_WRITE_BACK_DATA_PATH="${WOLF_SV_TEST_INPUT_DIR}/convert/write_back.sv"
)
register_test_exe(convert-write-back)

# convert: memory port lowerer
add_executable(convert-memory-port-lowerer
    tests/convert/test_convert_memory_port_lowerer.cpp
)
target_link_libraries(convert-memory-port-lowerer
    PRIVATE
        convert
)
target_compile_definitions(convert-memory-port-lowerer
    PRIVATE
        WOLF_SV_CONVERT_MEMORY_PORT_DATA_PATH="${WOLF_SV_TEST_INPUT_DIR}/convert/memory_ports.sv"
)
register_test_exe(convert-memory-port-lowerer)

# convert: graph assembly basic
add_executable(convert-graph-assembly-basic
    tests/convert/test_convert_graph_assembly_basic.cpp
)
target_link_libraries(convert-graph-assembly-basic
    PRIVATE
        convert
        grh
)
target_compile_definitions(convert-graph-assembly-basic
    PRIVATE
        WOLF_SV_CONVERT_GRAPH_ASSEMBLY_DATA_PATH="${WOLF_SV_TEST_INPUT_DIR}/convert/graph_assembly_basic.sv"
)
register_test_exe(convert-graph-assembly-basic)

# convert: graph assembly memory
add_executable(convert-graph-assembly-memory
    tests/convert/test_convert_graph_assembly_memory.cpp
)
target_link_libraries(convert-graph-assembly-memory
    PRIVATE
        convert
        grh
)
target_compile_definitions(convert-graph-assembly-memory
    PRIVATE
        WOLF_SV_CONVERT_GRAPH_ASSEMBLY_MEMORY_DATA_PATH="${WOLF_SV_TEST_INPUT_DIR}/convert/graph_assembly_memory.sv"
)
register_test_exe(convert-graph-assembly-memory)

# convert: graph assembly dpi/display
add_executable(convert-graph-assembly-dpi-display
    tests/convert/test_convert_graph_assembly_dpi_display.cpp
)
target_link_libraries(convert-graph-assembly-dpi-display
    PRIVATE
        convert
        grh
)
target_compile_definitions(convert-graph-assembly-dpi-display
    PRIVATE
        WOLF_SV_CONVERT_GRAPH_ASSEMBLY_DPI_DISPLAY_DATA_PATH="${WOLF_SV_TEST_INPUT_DIR}/convert/graph_assembly_dpi_display.sv"
)
register_test_exe(convert-graph-assembly-dpi-display)

# convert: graph assembly instance
add_executable(convert-graph-assembly-instance
    tests/convert/test_convert_graph_assembly_instance.cpp
)
target_link_libraries(convert-graph-assembly-instance
    PRIVATE
        convert
        grh
)
target_compile_definitions(convert-graph-assembly-instance
    PRIVATE
        WOLF_SV_CONVERT_GRAPH_ASSEMBLY_INSTANCE_DATA_PATH="${WOLF_SV_TEST_INPUT_DIR}/convert/graph_assembly_instance.sv"
)
register_test_exe(convert-graph-assembly-instance)

# Installation rules
install(TARGETS wolf-sv-parser DESTINATION bin)
