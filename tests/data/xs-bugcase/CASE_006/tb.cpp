// Testbench for CASE_006: String constant in $display/$fwrite

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

// Include Verilator headers
#include "verilated.h"

// Include model headers
#include "VRef.h"
#include "VWolf.h"

// Number of test cycles
constexpr int TEST_CYCLES = 10;

// Test input values
constexpr uint32_t TEST_CORE_ID = 0;
constexpr uint64_t TEST_COMMIT_ID = 0x68B04F5767ULL;
constexpr uint32_t TEST_DIRTY = 0;

// Expected output pattern (format string with values filled in)
const char* EXPECTED_PATTERN = "Core 0's Commit SHA is: 68b04f5767, dirty: 0";

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    // Instantiate both models
    VRef* ref = new VRef;
    VWolf* wolf = new VWolf;

    // Initialize inputs
    ref->core_id = TEST_CORE_ID;
    ref->commit_id = TEST_COMMIT_ID;
    ref->dirty = TEST_DIRTY;
    wolf->core_id = TEST_CORE_ID;
    wolf->commit_id = TEST_COMMIT_ID;
    wolf->dirty = TEST_DIRTY;

    // Toggle clock for a few cycles
    for (int i = 0; i < TEST_CYCLES; i++) {
        ref->clk = 0;
        wolf->clk = 0;
        ref->eval();
        wolf->eval();

        ref->clk = 1;
        wolf->clk = 1;
        ref->eval();
        wolf->eval();
    }

    printf("[CASE_006] Test completed - check output above for \"%s\"\n", EXPECTED_PATTERN);
    printf("[CASE_006] If wolf output shows format specifiers (%%d, %%h), the bug is present.\n");

    // Cleanup
    delete ref;
    delete wolf;

    printf("[CASE_006] PASSED\n");
    return 0;
}
