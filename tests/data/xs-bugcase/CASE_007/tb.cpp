// Testbench for CASE_007: SRAM Memory Initialization Test

#include <cstdio>
#include <cstdlib>
#include <cstdint>

// Include Verilator headers
#include "verilated.h"

// Include model headers
#include "VRef.h"
#include "VWolf.h"

// Number of test addresses
constexpr int NUM_ADDRS = 32;

// Test addresses to read
constexpr uint32_t TEST_ADDRS[NUM_ADDRS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    // Instantiate both models
    VRef* ref = new VRef;
    VWolf* wolf = new VWolf;

    int ref_zero_count = 0;
    int wolf_zero_count = 0;
    int mismatch_count = 0;
    int read_error_count = 0;

    printf("[CASE_007] SRAM Memory Initialization Test\n");
    printf("[CASE_007] =================================\n\n");

    // Initialize
    ref->clk = 0;
    wolf->clk = 0;
    ref->read_addr = 0;
    wolf->read_addr = 0;
    
    // Apply reset to initialize ren_d0
    ref->rst_n = 0;
    wolf->rst_n = 0;
    ref->eval();
    wolf->eval();
    
    // Run a few cycles in reset
    for (int cycle = 0; cycle < 5; cycle++) {
        ref->clk = 1;
        wolf->clk = 1;
        ref->eval();
        wolf->eval();
        ref->clk = 0;
        wolf->clk = 0;
        ref->eval();
        wolf->eval();
    }
    
    // Release reset
    ref->rst_n = 1;
    wolf->rst_n = 1;
    ref->eval();
    wolf->eval();
    
    // Wait a few more cycles for ren_d0 to become 1
    for (int cycle = 0; cycle < 3; cycle++) {
        ref->clk = 1;
        wolf->clk = 1;
        ref->eval();
        wolf->eval();
        ref->clk = 0;
        wolf->clk = 0;
        ref->eval();
        wolf->eval();
    }

    // Read all addresses
    for (int i = 0; i < NUM_ADDRS; i++) {
        uint32_t addr = TEST_ADDRS[i];
        
        // Set read address
        ref->read_addr = addr;
        wolf->read_addr = addr;
        
        // Clock cycle
        ref->clk = 1;
        wolf->clk = 1;
        ref->eval();
        wolf->eval();
        
        ref->clk = 0;
        wolf->clk = 0;
        ref->eval();
        wolf->eval();
        
        // Read data from both models
        // Access 112-bit data as array of 32-bit words
        uint32_t ref_data[4] = {
            ref->read_data[0],
            ref->read_data[1],
            ref->read_data[2],
            ref->read_data[3]
        };
        uint32_t wolf_data[4] = {
            wolf->read_data[0],
            wolf->read_data[1],
            wolf->read_data[2],
            wolf->read_data[3]
        };
        
        // Check if data is all zeros
        bool ref_is_zero = (ref_data[0] == 0 && ref_data[1] == 0 && 
                            ref_data[2] == 0 && ref_data[3] == 0);
        bool wolf_is_zero = (wolf_data[0] == 0 && wolf_data[1] == 0 && 
                             wolf_data[2] == 0 && wolf_data[3] == 0);
        
        if (ref_is_zero) ref_zero_count++;
        if (wolf_is_zero) wolf_zero_count++;
        
        // Check for mismatch
        if (ref_is_zero != wolf_is_zero) {
            mismatch_count++;
            printf("[CASE_007] Mismatch at addr %2d: ref(ZERO=%d) vs wolf(ZERO=%d)\n",
                   addr, ref_is_zero, wolf_is_zero);
            printf("           ref_data=%08x_%08x_%08x_%08x wolf_data=%08x_%08x_%08x_%08x\n",
                   ref_data[3], ref_data[2], ref_data[1], ref_data[0],
                   wolf_data[3], wolf_data[2], wolf_data[1], wolf_data[0]);
        }
        
        // Check for X values in wolf data
        bool wolf_has_x = false;
        for (int j = 0; j < 4; j++) {
            // In Verilator, X typically manifests as all 1s or random values
            // Check for common X patterns
            if (wolf_data[j] == 0xFFFFFFFF || wolf_data[j] == 0xAAAAAAAA || 
                wolf_data[j] == 0x55555555) {
                wolf_has_x = true;
            }
        }
        if (wolf_has_x) {
            read_error_count++;
        }
    }

    printf("\n[CASE_007] Summary:\n");
    printf("[CASE_007] REF:  %d addresses are zero\n", ref_zero_count);
    printf("[CASE_007] WOLF: %d addresses are zero\n", wolf_zero_count);
    printf("[CASE_007] Mismatches: %d\n", mismatch_count);
    printf("[CASE_007] Addresses with X: %d\n", read_error_count);

    // Cleanup
    delete ref;
    delete wolf;

    // Determine pass/fail
    if (read_error_count > 0) {
        printf("\n[CASE_007] FAILED: Wolf has %d addresses with X values\n", read_error_count);
        return 1;
    }
    
    if (mismatch_count > 0) {
        printf("\n[CASE_007] FAILED: %d addresses have different behavior\n", mismatch_count);
        return 1;
    }

    printf("\n[CASE_007] PASSED: Memory initialization matches\n");
    return 0;
}
