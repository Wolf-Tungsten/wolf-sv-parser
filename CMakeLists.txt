cmake_minimum_required(VERSION 3.20)
project(wolvrix VERSION 0.1.0 LANGUAGES CXX)

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

# Ensure static dependencies can link into shared libs.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Output directories (avoid per-target duplication)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Ensure important runtime/artifact directories exist at configure time
file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_subdirectory(external/slang)

# Libraries
set(WOLVRIX_LIB_SOURCES
    lib/src/diagnostics.cpp
    lib/src/grh.cpp
    lib/src/load.cpp
    lib/src/emit.cpp
    lib/src/store.cpp
    lib/src/ingest.cpp
    lib/src/transform.cpp
    lib/transform/demo_stats.cpp
    lib/transform/const_fold.cpp
    lib/transform/redundant_elim.cpp
    lib/transform/dead_code_elim.cpp
    lib/transform/memory_init_check.cpp
    lib/transform/xmr_resolve.cpp
)

add_library(wolvrix-lib SHARED ${WOLVRIX_LIB_SOURCES})

target_include_directories(wolvrix-lib
    PUBLIC
        lib/include
)

target_link_libraries(wolvrix-lib
    PUBLIC
        slang::slang
)

add_library(wolvrix::lib ALIAS wolvrix-lib)

# Application
add_executable(wolvrix-cli
    app/cli/main.cpp
)

target_link_libraries(wolvrix-cli
    PRIVATE
        wolvrix-lib
)

enable_testing()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
# Common test paths
set(WOLF_SV_TEST_INPUT_DIR "${CMAKE_SOURCE_DIR}/tests/data")
set(WOLF_SV_INGEST_TEST_DATA_DIR "${CMAKE_SOURCE_DIR}/tests/ingest/data")
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
        wolvrix-lib
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
        wolvrix-lib
)

register_test_exe(grh-roundtrip-tests)

# grh symbol utils tests
add_executable(grh-symbol-utils-tests
    tests/grh/test_grh_symbol_utils.cpp
)

target_link_libraries(grh-symbol-utils-tests
    PRIVATE
        wolvrix-lib
)

register_test_exe(grh-symbol-utils-tests)

# emit base
add_executable(emit-base
    tests/emit/test_emit_base.cpp
)

target_link_libraries(emit-base
    PRIVATE
        wolvrix-lib
)

target_compile_definitions(emit-base
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
)

register_test_exe(emit-base)

add_executable(store-json
    tests/store/test_store_json.cpp
)

target_link_libraries(store-json
    PRIVATE
        wolvrix-lib
)

target_compile_definitions(store-json
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
)

register_test_exe(store-json)

add_executable(store-json-ir
    tests/store/test_store_json_ir.cpp
)

target_link_libraries(store-json-ir
    PRIVATE
        wolvrix-lib
)

target_compile_definitions(store-json-ir
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
)

register_test_exe(store-json-ir)

add_executable(emit-sv-readmem
    tests/emit/test_emit_sv_readmem.cpp
)

target_link_libraries(emit-sv-readmem
    PRIVATE
        wolvrix-lib
)

target_compile_definitions(emit-sv-readmem
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
)

register_test_exe(emit-sv-readmem)

add_executable(emit-sv-storage-ports
    tests/emit/test_emit_sv_storage_ports.cpp
)

target_link_libraries(emit-sv-storage-ports
    PRIVATE
        wolvrix-lib
)

target_compile_definitions(emit-sv-storage-ports
    PRIVATE
        WOLF_SV_EMIT_ARTIFACT_DIR="${EMIT_ARTIFACT_DIR}"
)

register_test_exe(emit-sv-storage-ports)

# transform tests
add_executable(transform-pass-manager
    tests/transform/test_transform_pass_manager.cpp
)

target_link_libraries(transform-pass-manager
    PRIVATE
        wolvrix-lib
)

register_test_exe(transform-pass-manager)

add_executable(transform-const-fold
    tests/transform/test_const_fold_pass.cpp
)

target_link_libraries(transform-const-fold
    PRIVATE
        wolvrix-lib
)

register_test_exe(transform-const-fold)

add_executable(transform-dead-code-elim
    tests/transform/test_dead_code_elim_pass.cpp
)

target_link_libraries(transform-dead-code-elim
    PRIVATE
        wolvrix-lib
)

register_test_exe(transform-dead-code-elim)

add_executable(transform-redundant-elim
    tests/transform/test_redundant_elim_pass.cpp
)

target_link_libraries(transform-redundant-elim
    PRIVATE
        wolvrix-lib
)

register_test_exe(transform-redundant-elim)

add_executable(transform-xmr-resolve-inout
    tests/transform/test_xmr_resolve_inout.cpp
)

target_link_libraries(transform-xmr-resolve-inout
    PRIVATE
        wolvrix-lib
)

register_test_exe(transform-xmr-resolve-inout)

add_executable(transform-xmr-resolve-storage
    tests/transform/test_xmr_resolve_storage.cpp
)

target_link_libraries(transform-xmr-resolve-storage
    PRIVATE
        wolvrix-lib
)

register_test_exe(transform-xmr-resolve-storage)

# ingest: symbol collector
add_executable(ingest-symbol-collector
    tests/ingest/test_ingest_symbol_collector.cpp
)

target_link_libraries(ingest-symbol-collector
    PRIVATE
        wolvrix-lib
)

target_compile_definitions(ingest-symbol-collector
    PRIVATE
        WOLF_SV_INGEST_HDLBITS_DUT_DIR="${WOLF_SV_TEST_INPUT_DIR}/hdlbits/dut"
)

register_test_exe(ingest-symbol-collector)

# ingest: type resolver
add_executable(ingest-type-resolver
    tests/ingest/test_ingest_type_resolver.cpp
)

target_link_libraries(ingest-type-resolver
    PRIVATE
        wolvrix-lib
)

target_compile_definitions(ingest-type-resolver
    PRIVATE
        WOLF_SV_INGEST_HDLBITS_DUT_DIR="${WOLF_SV_TEST_INPUT_DIR}/hdlbits/dut"
)

register_test_exe(ingest-type-resolver)

# ingest: stmt lowerer
add_executable(ingest-stmt-lowerer
    tests/ingest/test_ingest_stmt_lowerer.cpp
)
target_link_libraries(ingest-stmt-lowerer
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-stmt-lowerer
    PRIVATE
        WOLF_SV_INGEST_STMT_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/stmt_lowerer.sv"
)
register_test_exe(ingest-stmt-lowerer)

# ingest: stmt lowerer error paths
add_executable(ingest-stmt-lowerer-errors
    tests/ingest/test_ingest_stmt_lowerer_errors.cpp
)
target_link_libraries(ingest-stmt-lowerer-errors
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-stmt-lowerer-errors
    PRIVATE
        WOLF_SV_INGEST_STMT_ERROR_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/stmt_lowerer_errors.sv"
)
register_test_exe(ingest-stmt-lowerer-errors)

# ingest: write-back
add_executable(ingest-write-back
    tests/ingest/test_ingest_write_back.cpp
)
target_link_libraries(ingest-write-back
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-write-back
    PRIVATE
        WOLF_SV_INGEST_WRITE_BACK_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/write_back.sv"
)
register_test_exe(ingest-write-back)

# ingest: write-back slice
add_executable(ingest-write-back-slice
    tests/ingest/test_ingest_write_back_slice.cpp
)
target_link_libraries(ingest-write-back-slice
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-write-back-slice
    PRIVATE
        WOLF_SV_INGEST_WRITE_BACK_SLICE_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/write_back_slice.sv"
)
register_test_exe(ingest-write-back-slice)

# ingest: memory port lowerer
add_executable(ingest-memory-port-lowerer
    tests/ingest/test_ingest_memory_port_lowerer.cpp
)
target_link_libraries(ingest-memory-port-lowerer
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-memory-port-lowerer
    PRIVATE
        WOLF_SV_INGEST_MEMORY_PORT_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/memory_ports.sv"
)
register_test_exe(ingest-memory-port-lowerer)

# ingest: graph assembly basic
add_executable(ingest-graph-assembly-basic
    tests/ingest/test_ingest_graph_assembly_basic.cpp
)
target_link_libraries(ingest-graph-assembly-basic
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-graph-assembly-basic
    PRIVATE
        WOLF_SV_INGEST_GRAPH_ASSEMBLY_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/graph_assembly_basic.sv"
)
register_test_exe(ingest-graph-assembly-basic)

# ingest: graph assembly register multi-write
add_executable(ingest-graph-assembly-register-multi
    tests/ingest/test_ingest_graph_assembly_register_multi.cpp
)
target_link_libraries(ingest-graph-assembly-register-multi
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-graph-assembly-register-multi
    PRIVATE
        WOLF_SV_INGEST_GRAPH_ASSEMBLY_REGISTER_MULTI_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/graph_assembly_register_multi.sv"
)
register_test_exe(ingest-graph-assembly-register-multi)

# ingest: graph assembly memory
add_executable(ingest-graph-assembly-memory
    tests/ingest/test_ingest_graph_assembly_memory.cpp
)
target_link_libraries(ingest-graph-assembly-memory
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-graph-assembly-memory
    PRIVATE
        WOLF_SV_INGEST_GRAPH_ASSEMBLY_MEMORY_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/graph_assembly_memory.sv"
)
register_test_exe(ingest-graph-assembly-memory)

# ingest: graph assembly slice
add_executable(ingest-graph-assembly-slice
    tests/ingest/test_ingest_graph_assembly_slice.cpp
)
target_link_libraries(ingest-graph-assembly-slice
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-graph-assembly-slice
    PRIVATE
        WOLF_SV_INGEST_GRAPH_ASSEMBLY_SLICE_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/graph_assembly_slice.sv"
)
register_test_exe(ingest-graph-assembly-slice)

# ingest: graph assembly dpi/display
add_executable(ingest-graph-assembly-dpi-display
    tests/ingest/test_ingest_graph_assembly_dpi_display.cpp
)
target_link_libraries(ingest-graph-assembly-dpi-display
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-graph-assembly-dpi-display
    PRIVATE
        WOLF_SV_INGEST_GRAPH_ASSEMBLY_DPI_DISPLAY_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/graph_assembly_dpi_display.sv"
)
register_test_exe(ingest-graph-assembly-dpi-display)

# ingest: graph assembly instance
add_executable(ingest-graph-assembly-instance
    tests/ingest/test_ingest_graph_assembly_instance.cpp
)
target_link_libraries(ingest-graph-assembly-instance
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-graph-assembly-instance
    PRIVATE
        WOLF_SV_INGEST_GRAPH_ASSEMBLY_INSTANCE_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/graph_assembly_instance.sv"
)
register_test_exe(ingest-graph-assembly-instance)

# ingest: graph assembly instance dedup
add_executable(ingest-graph-assembly-instance-dedup
    tests/ingest/test_ingest_graph_assembly_instance_dedup.cpp
)
target_link_libraries(ingest-graph-assembly-instance-dedup
    PRIVATE
        wolvrix-lib
)
target_compile_definitions(ingest-graph-assembly-instance-dedup
    PRIVATE
        WOLF_SV_INGEST_GRAPH_ASSEMBLY_INSTANCE_DEDUP_DATA_PATH="${WOLF_SV_INGEST_TEST_DATA_DIR}/graph_assembly_instance_dedup.sv"
)
register_test_exe(ingest-graph-assembly-instance-dedup)

# Installation rules
install(TARGETS wolvrix-cli RUNTIME DESTINATION bin)
install(TARGETS wolvrix-lib LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
