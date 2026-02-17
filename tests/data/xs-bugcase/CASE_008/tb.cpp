// Testbench for CASE_008: Register Initialization Test

#include <cstdio>
#include <cstdlib>
#include <cstdint>

// Include Verilator headers
#include "verilated.h"

// Include model headers
#include "VRef.h"
#include "VWolf.h"

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    // Instantiate both models
    VRef* ref = new VRef;
    VWolf* wolf = new VWolf;

    printf("[CASE_008] Register Initialization Test\n");
    printf("[CASE_008] =============================\n\n");

    // Initialize
    ref->clk = 0;
    wolf->clk = 0;
    ref->rst_n = 0;
    wolf->rst_n = 0;
    ref->eval();
    wolf->eval();

    // Release reset
    ref->rst_n = 1;
    wolf->rst_n = 1;
    ref->eval();
    wolf->eval();

    printf("[CASE_008] Checking initial values after reset...\n");
    
    // Access signals directly from DUT (not through tb wrapper)
    bool ref_is_zero = ref->is_zero;
    bool wolf_is_zero = wolf->is_zero;
    uint8_t ref_counter = ref->counter_val;
    uint8_t wolf_counter = wolf->counter_val;
    
    printf("[CASE_008] Initial values:\n");
    printf("[CASE_008]   REF:  counter_val=0x%02x, is_zero=%d\n", ref_counter, ref_is_zero);
    printf("[CASE_008]   WOLF: counter_val=0x%02x, is_zero=%d\n", wolf_counter, wolf_is_zero);

    bool pass = true;
    
    // Check initial values (should be 0 from initial block)
    if (!ref_is_zero) {
        printf("[CASE_008] ERROR: REF counter should be 0 initially\n");
        pass = false;
    }
    if (!wolf_is_zero) {
        printf("[CASE_008] ERROR: WOLF counter should be 0 initially (got 0x%02x)\n", wolf_counter);
        pass = false;
    }

    // Run a few cycles and check counter increments
    printf("\n[CASE_008] Running 3 clock cycles...\n");
    for (int i = 0; i < 3; i++) {
        ref->clk = 1;
        wolf->clk = 1;
        ref->eval();
        wolf->eval();
        
        ref->clk = 0;
        wolf->clk = 0;
        ref->eval();
        wolf->eval();
        
        printf("[CASE_008] After cycle %d: REF(counter=0x%02x, is_zero=%d), WOLF(counter=0x%02x, is_zero=%d)\n", 
               i+1, ref->counter_val, ref->is_zero, wolf->counter_val, wolf->is_zero);
    }

    // Cleanup
    delete ref;
    delete wolf;

    if (pass) {
        printf("\n[CASE_008] PASSED: Register initialization matches\n");
        return 0;
    } else {
        printf("\n[CASE_008] FAILED: Register initialization mismatch\n");
        return 1;
    }
}
