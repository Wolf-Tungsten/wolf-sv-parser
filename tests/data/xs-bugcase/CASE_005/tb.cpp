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

static int compare_step(const VRef *ref, const VWolf *wolf, int cycle) {
    const uint8_t expected_id = 1; // idx=1 should map to 1
    const uint32_t expected_sel = 1u << expected_id;

    if (ref->id_shift != expected_id || ref->id_port != expected_id || ref->bad != 0 || ref->sel != expected_sel) {
        std::fprintf(stderr,
                     "[REF-UNEXPECTED] cycle=%d id_shift=%u id_port=%u sel=0x%08x bad=%u\n",
                     cycle,
                     static_cast<unsigned>(ref->id_shift),
                     static_cast<unsigned>(ref->id_port),
                     static_cast<unsigned>(ref->sel),
                     static_cast<unsigned>(ref->bad));
        return 1;
    }
    if (ref->id_shift != wolf->id_shift || ref->id_port != wolf->id_port ||
        ref->sel != wolf->sel || ref->bad != wolf->bad) {
        std::fprintf(stderr,
                     "[MISMATCH] cycle=%d id_shift ref=%u wolf=%u id_port ref=%u wolf=%u sel ref=0x%08x wolf=0x%08x bad ref=%u wolf=%u\n",
                     cycle,
                     static_cast<unsigned>(ref->id_shift),
                     static_cast<unsigned>(wolf->id_shift),
                     static_cast<unsigned>(ref->id_port),
                     static_cast<unsigned>(wolf->id_port),
                     static_cast<unsigned>(ref->sel),
                     static_cast<unsigned>(wolf->sel),
                     static_cast<unsigned>(ref->bad),
                     static_cast<unsigned>(wolf->bad));
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
    ref->idx = 0;
    wolf->idx = 0;

    for (int i = 0; i < 2; ++i) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
    }

    ref->rst_n = 1;
    wolf->rst_n = 1;
    ref->idx = 1;
    wolf->idx = 1;

    int cycle = 0;
    for (int i = 0; i < 4; ++i) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
        if (compare_step(ref, wolf, cycle++) != 0) {
            delete ref;
            delete wolf;
            return 1;
        }
    }

    delete ref;
    delete wolf;
    return 0;
}
