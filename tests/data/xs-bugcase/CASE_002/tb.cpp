#include <cstdint>
#include <cstdio>

#include "svdpi.h"
#include "VRef.h"
#include "VWolf.h"
#include "verilated.h"
#include "verilated_cov.h"

extern "C" long long difftest_ram_read(long long rIdx) {
    return rIdx ^ 0x5a5a5a5a5a5a5a5aLL;
}

extern "C" void difftest_ram_write(long long index, long long data, long long mask) {
    (void)index;
    (void)data;
    (void)mask;
}

extern "C" int jtag_tick(svBit *jtag_TCK,
                          svBit *jtag_TMS,
                          svBit *jtag_TDI,
                          svBit *jtag_TRSTn,
                          svBit jtag_TDO) {
    static int tick_counts[2] = {0, 0};
    extern int g_model_index;
    int &tick_count = tick_counts[g_model_index];
    const int value = tick_count ^ (jtag_TDO ? 1 : 0);
    *jtag_TCK = (tick_count >> 0) & 1;
    *jtag_TMS = (tick_count >> 1) & 1;
    *jtag_TDI = (tick_count >> 2) & 1;
    *jtag_TRSTn = (tick_count >> 3) & 1;
    ++tick_count;
    return value;
}

static vluint64_t main_time = 0;
double sc_time_stamp() { return static_cast<double>(main_time); }

int g_model_index = 0;

static void tick(VRef *ref, VWolf *wolf, bool clk) {
    ref->clk = clk;
    wolf->clk = clk;
    g_model_index = 0;
    ref->eval();
    g_model_index = 1;
    wolf->eval();
    ++main_time;
}

static int compare_step(const VRef *ref, const VWolf *wolf, int cycle) {
    if (ref->r_0_data != wolf->r_0_data) {
        std::fprintf(stderr,
                     "[MISMATCH] cycle=%d r_0_data ref=%016llx wolf=%016llx\n",
                     cycle,
                     static_cast<unsigned long long>(ref->r_0_data),
                     static_cast<unsigned long long>(wolf->r_0_data));
        return 1;
    }
    if (ref->r_0_async != wolf->r_0_async) {
        std::fprintf(stderr,
                     "[MISMATCH] cycle=%d r_0_async ref=%u wolf=%u\n",
                     cycle,
                     static_cast<unsigned>(ref->r_0_async),
                     static_cast<unsigned>(wolf->r_0_async));
        return 1;
    }
    if (ref->jtag_TCK != wolf->jtag_TCK || ref->jtag_TMS != wolf->jtag_TMS ||
        ref->jtag_TDI != wolf->jtag_TDI || ref->jtag_TRSTn != wolf->jtag_TRSTn) {
        std::fprintf(stderr,
                     "[MISMATCH] cycle=%d jtag ref=%u%u%u%u wolf=%u%u%u%u\n",
                     cycle,
                     static_cast<unsigned>(ref->jtag_TCK),
                     static_cast<unsigned>(ref->jtag_TMS),
                     static_cast<unsigned>(ref->jtag_TDI),
                     static_cast<unsigned>(ref->jtag_TRSTn),
                     static_cast<unsigned>(wolf->jtag_TCK),
                     static_cast<unsigned>(wolf->jtag_TMS),
                     static_cast<unsigned>(wolf->jtag_TDI),
                     static_cast<unsigned>(wolf->jtag_TRSTn));
        return 1;
    }
    if (ref->exit != wolf->exit) {
        std::fprintf(stderr,
                     "[MISMATCH] cycle=%d exit ref=%u wolf=%u\n",
                     cycle,
                     static_cast<unsigned>(ref->exit),
                     static_cast<unsigned>(wolf->exit));
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

    ref->enable = 0;
    wolf->enable = 0;
    ref->init_done = 0;
    wolf->init_done = 0;
    ref->r_0_enable = 0;
    wolf->r_0_enable = 0;
    ref->r_0_index = 0;
    wolf->r_0_index = 0;
    ref->w_0_enable = 0;
    wolf->w_0_enable = 0;
    ref->w_0_index = 0;
    wolf->w_0_index = 0;
    ref->w_0_data = 0;
    wolf->w_0_data = 0;
    ref->w_0_mask = 0;
    wolf->w_0_mask = 0;
    ref->jtag_TDO_data = 0;
    wolf->jtag_TDO_data = 0;
    ref->jtag_TDO_driven = 0;
    wolf->jtag_TDO_driven = 0;

    for (int i = 0; i < 3; ++i) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
    }

    ref->rst_n = 1;
    wolf->rst_n = 1;

    ref->enable = 1;
    wolf->enable = 1;

    const int max_cycles = 16;
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        ref->init_done = (cycle >= 1) ? 1 : 0;
        wolf->init_done = (cycle >= 1) ? 1 : 0;

        ref->r_0_enable = (cycle & 1) ? 1 : 0;
        wolf->r_0_enable = ref->r_0_enable;
        ref->r_0_index = static_cast<vluint64_t>(cycle & 0x7);
        wolf->r_0_index = ref->r_0_index;

        ref->w_0_enable = (cycle % 3 == 0) ? 1 : 0;
        wolf->w_0_enable = ref->w_0_enable;
        ref->w_0_index = static_cast<vluint64_t>((cycle + 1) & 0x7);
        wolf->w_0_index = ref->w_0_index;
        ref->w_0_data = 0x100u + static_cast<vluint64_t>(cycle);
        wolf->w_0_data = ref->w_0_data;
        ref->w_0_mask = 0xffffffffffffffffULL;
        wolf->w_0_mask = ref->w_0_mask;

        ref->jtag_TDO_driven = 1;
        wolf->jtag_TDO_driven = 1;
        ref->jtag_TDO_data = (cycle & 1) ? 1 : 0;
        wolf->jtag_TDO_data = ref->jtag_TDO_data;

        tick(ref, wolf, 0);
        tick(ref, wolf, 1);

        if (compare_step(ref, wolf, cycle) != 0) {
            delete ref;
            delete wolf;
            return 1;
        }
    }

    delete ref;
    delete wolf;
    VerilatedCov::write();
    return 0;
}
