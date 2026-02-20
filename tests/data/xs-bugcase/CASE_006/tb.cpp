#include <cstdint>
#include <cstdio>
#include <unordered_map>

#include "svdpi.h"
#include "VRef.h"
#include "VWolf.h"
#include "verilated.h"
#if defined(TRACE)
#include "verilated_fst_c.h"
#endif

static uint64_t init_value(uint64_t idx) {
    uint64_t x = idx ^ 0x9e3779b97f4a7c15ULL;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static std::unordered_map<uint64_t, uint64_t> g_mem[2];

int g_model_index = 0;

extern "C" long long difftest_ram_read(long long rIdx) {
    const uint64_t idx = static_cast<uint64_t>(rIdx);
    auto &mem = g_mem[g_model_index];
    auto it = mem.find(idx);
    uint64_t data = 0;
    if (it != mem.end()) {
        data = it->second;
    } else {
        data = init_value(idx);
    }
    std::fprintf(stdout,
                 "[DPIC] model=%d read idx=0x%016llx data=0x%016llx\n",
                 g_model_index,
                 static_cast<unsigned long long>(idx),
                 static_cast<unsigned long long>(data));
    return static_cast<long long>(data);
}

extern "C" void difftest_ram_write(long long index, long long data, long long mask) {
    const uint64_t idx = static_cast<uint64_t>(index);
    auto &mem = g_mem[g_model_index];
    uint64_t cur = init_value(idx);
    auto it = mem.find(idx);
    if (it != mem.end()) {
        cur = it->second;
    }
    const uint64_t d = static_cast<uint64_t>(data);
    const uint64_t m = static_cast<uint64_t>(mask);
    mem[idx] = (cur & ~m) | (d & m);
}

static vluint64_t main_time = 0;

double sc_time_stamp() { return static_cast<double>(main_time); }

#if defined(TRACE)
static VerilatedFstC *trace_all = nullptr;
#endif

static void tick(VRef *ref, VWolf *wolf, bool clk) {
    ref->clk = clk;
    wolf->clk = clk;

    g_model_index = 0;
    ref->eval();
    g_model_index = 1;
    wolf->eval();

#if defined(TRACE)
    if (trace_all) {
        trace_all->dump(main_time);
    }
#endif

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
    return 0;
}

static void init_ports(VRef *ref, VWolf *wolf) {
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
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);

    VRef *ref = new VRef;
    VWolf *wolf = new VWolf;

#if defined(TRACE)
    Verilated::traceEverOn(true);
    trace_all = new VerilatedFstC;
    ref->trace(trace_all, 99);
    wolf->trace(trace_all, 99);
    trace_all->open("case006.fst");
#endif

    ref->clk = 0;
    wolf->clk = 0;
    ref->rst_n = 0;
    wolf->rst_n = 0;

    init_ports(ref, wolf);

    for (int i = 0; i < 2; ++i) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
    }

    ref->rst_n = 1;
    wolf->rst_n = 1;

    const int cycles = 5000;
    uint64_t rng = 0x9e3779b97f4a7c15ULL;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        const uint64_t idx = static_cast<uint64_t>(cycle & 0x0fff);
        const bool r_en = (rng & 0x3) != 0;
        const bool w_en = false;
        const uint64_t w_data = 0;
        const uint64_t w_mask = 0;

        ref->r_0_enable = r_en;
        ref->r_0_index = idx;
        ref->w_0_enable = w_en;
        ref->w_0_index = idx;
        ref->w_0_data = w_data;
        ref->w_0_mask = w_mask;
        wolf->r_0_enable = r_en;
        wolf->r_0_index = idx;
        wolf->w_0_enable = w_en;
        wolf->w_0_index = idx;
        wolf->w_0_data = w_data;
        wolf->w_0_mask = w_mask;

        tick(ref, wolf, 0);
        tick(ref, wolf, 1);

        std::fprintf(stdout,
                     "[TB-TRACE] cycle=%d ref_en=%u ref_idx=0x%016llx ref_data=0x%016llx wolf_en=%u wolf_idx=0x%016llx wolf_data=0x%016llx\n",
                     cycle,
                     static_cast<unsigned>(ref->r_0_enable),
                     static_cast<unsigned long long>(ref->r_0_index),
                     static_cast<unsigned long long>(ref->r_0_data),
                     static_cast<unsigned>(wolf->r_0_enable),
                     static_cast<unsigned long long>(wolf->r_0_index),
                     static_cast<unsigned long long>(wolf->r_0_data));

        if (compare_step(ref, wolf, cycle) != 0) {
            delete ref;
            delete wolf;
            return 1;
        }
    }

    delete ref;
    delete wolf;

#if defined(TRACE)
    if (trace_all) {
        trace_all->close();
        delete trace_all;
    }
#endif

    return 0;
}
