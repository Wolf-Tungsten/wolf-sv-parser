#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "VRef.h"
#include "VWolf.h"
#include "verilated.h"
#include "verilated_cov.h"

static int xs_assert_count = 0;

extern "C" void xs_assert(long long line) {
    if (xs_assert_count < 10) {
        std::fprintf(stderr, "Assertion failed at line %lld.\n", line);
        ++xs_assert_count;
    }
}

extern "C" void xs_assert_v2(const char *filename, long long line) {
    if (xs_assert_count < 10) {
        std::fprintf(stderr, "Assertion failed at %s:%lld.\n", filename, line);
        ++xs_assert_count;
    }
}

static vluint64_t main_time = 0;
double sc_time_stamp() { return static_cast<double>(main_time); }

static void tick(VRef *ref, VWolf *wolf, bool clk) {
    ref->clk = clk;
    wolf->clk = clk;
    ref->eval();
    wolf->eval();
    ++main_time;
}

static void write_coverage() {
    const char *cov = std::getenv("VERILATOR_COV_FILE");
    if (cov && cov[0]) {
        VerilatedCov::write(cov);
    }
}

static int compare_step(const VRef *ref, const VWolf *wolf, int cycle) {
    if (ref->io_toFtq_prediction_ready_o != wolf->io_toFtq_prediction_ready_o) {
        std::fprintf(stderr, "[MISMATCH] cycle=%d io_toFtq_prediction_ready ref=%u wolf=%u\n",
                     cycle, ref->io_toFtq_prediction_ready_o, wolf->io_toFtq_prediction_ready_o);
        return 1;
    }
    if (ref->s1_fire_o != wolf->s1_fire_o) {
        std::fprintf(stderr, "[MISMATCH] cycle=%d s1_fire ref=%u wolf=%u\n",
                     cycle, ref->s1_fire_o, wolf->s1_fire_o);
        return 1;
    }
    if (ref->abtb_io_stageCtrl_s0_fire_probe_o != wolf->abtb_io_stageCtrl_s0_fire_probe_o) {
        std::fprintf(stderr, "[MISMATCH] cycle=%d abtb_io_stageCtrl_s0_fire_probe ref=%u wolf=%u\n",
                     cycle, ref->abtb_io_stageCtrl_s0_fire_probe_o, wolf->abtb_io_stageCtrl_s0_fire_probe_o);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::randReset(0);
    Verilated::randSeed(1);

    VRef *ref = new VRef;
    VWolf *wolf = new VWolf;

    ref->clk = 0;
    wolf->clk = 0;
    ref->rst_n = 0;
    wolf->rst_n = 0;

    const int reset_cycles = 5;
    for (int i = 0; i < reset_cycles; ++i) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
    }

    ref->rst_n = 1;
    wolf->rst_n = 1;

    const int warmup_cycles = 20;
    int status = 0;
    const int max_cycles = 5000;
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);

        if (cycle < warmup_cycles) {
            continue;
        }
        if (compare_step(ref, wolf, cycle) != 0) {
            status = 1;
            break;
        }

        if (Verilated::gotFinish()) {
            break;
        }
    }

    write_coverage();

    delete ref;
    delete wolf;
    return status;
}
