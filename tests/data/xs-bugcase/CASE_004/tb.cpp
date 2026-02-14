#include <cstdint>
#include <cstdio>

#include "VRef.h"
#include "VWolf.h"
#include "verilated.h"
#include "verilated_cov.h"

static vluint64_t main_time = 0;

double sc_time_stamp() { return static_cast<double>(main_time); }

static void tick(VRef *ref, VWolf *wolf, bool clk) {
    ref->clk = clk;
    wolf->clk = clk;
    ref->eval();
    wolf->eval();
    ++main_time;
}

static int compare_step(const VRef *ref, const VWolf *wolf, int cycle, uint16_t expected_sum) {
    if (ref->sum != expected_sum || ref->bad != 0) {
        std::fprintf(stderr,
                     "[REF-UNEXPECTED] cycle=%d sum=%u bad=%u expected_sum=%u expected_bad=0\n",
                     cycle, static_cast<unsigned>(ref->sum),
                     static_cast<unsigned>(ref->bad), expected_sum);
        return 1;
    }
    if (ref->sum != wolf->sum || ref->bad != wolf->bad) {
        std::fprintf(stderr,
                     "[MISMATCH] cycle=%d sum ref=%u wolf=%u bad ref=%u wolf=%u\n",
                     cycle,
                     static_cast<unsigned>(ref->sum), static_cast<unsigned>(wolf->sum),
                     static_cast<unsigned>(ref->bad), static_cast<unsigned>(wolf->bad));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);

    VRef *ref = new VRef;
    VWolf *wolf = new VWolf;

    ref->clk = 0;
    wolf->clk = 0;
    ref->rst_n = 0;
    wolf->rst_n = 0;
    ref->flag_a = 0;
    wolf->flag_a = 0;
    ref->flag_b = 0;
    wolf->flag_b = 0;
    ref->val_a = 0;
    wolf->val_a = 0;
    ref->val_b = 0;
    wolf->val_b = 0;
    ref->b0 = 0;
    wolf->b0 = 0;
    ref->b1 = 0;
    wolf->b1 = 0;
    ref->b2 = 0;
    wolf->b2 = 0;

    for (int i = 0; i < 2; ++i) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
    }

    ref->rst_n = 1;
    wolf->rst_n = 1;
    ref->flag_a = 0;
    wolf->flag_a = 0;
    ref->flag_b = 0;
    wolf->flag_b = 0;
    ref->val_a = 0xDF;
    wolf->val_a = 0xDF;
    ref->val_b = 0x00;
    wolf->val_b = 0x00;
    ref->b0 = 0x01;
    wolf->b0 = 0x01;
    ref->b1 = 0x00;
    wolf->b1 = 0x00;
    ref->b2 = 0x00;
    wolf->b2 = 0x00;

    const uint16_t expected_sum = 0xDF + 0x01;
    int cycle = 0;
    for (int i = 0; i < 4; ++i) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
        if (compare_step(ref, wolf, cycle++, expected_sum) != 0) {
            delete ref;
            delete wolf;
            return 1;
        }
    }

    delete ref;
    delete wolf;
    return 0;
}
