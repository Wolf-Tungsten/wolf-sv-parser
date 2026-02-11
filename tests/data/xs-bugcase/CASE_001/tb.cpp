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

static inline void set_w76(vluint32_t *dst, uint64_t lo, uint32_t hi) {
    dst[0] = static_cast<vluint32_t>(lo & 0xffffffffu);
    dst[1] = static_cast<vluint32_t>((lo >> 32) & 0xffffffffu);
    dst[2] = static_cast<vluint32_t>(hi & 0x0fffu);
}

static inline bool eq_w76(const vluint32_t *a, const vluint32_t *b) {
    if (a[0] != b[0]) {
        return false;
    }
    if (a[1] != b[1]) {
        return false;
    }
    return (a[2] & 0x0fffu) == (b[2] & 0x0fffu);
}

static int compare_step(const VRef *ref, const VWolf *wolf, int cycle) {
    if (!eq_w76(ref->RW0_rdata, wolf->RW0_rdata)) {
        std::fprintf(stderr,
                     "[MISMATCH] cycle=%d rdata ref=%08x_%08x_%03x wolf=%08x_%08x_%03x\n",
                     cycle,
                     ref->RW0_rdata[2] & 0x0fffu, ref->RW0_rdata[1], ref->RW0_rdata[0],
                     wolf->RW0_rdata[2] & 0x0fffu, wolf->RW0_rdata[1], wolf->RW0_rdata[0]);
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

    ref->RW0_en = 0;
    wolf->RW0_en = 0;
    ref->RW0_wmode = 0;
    wolf->RW0_wmode = 0;
    ref->RW0_addr = 0;
    wolf->RW0_addr = 0;
    set_w76(ref->RW0_wmask, 0, 0);
    set_w76(wolf->RW0_wmask, 0, 0);
    set_w76(ref->RW0_wdata, 0, 0);
    set_w76(wolf->RW0_wdata, 0, 0);

    for (int i = 0; i < 2; ++i) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
    }

    ref->rst_n = 1;
    wolf->rst_n = 1;

    const uint64_t wmask_lo = 0xffffffffffffffffULL;
    const uint32_t wmask_hi = 0x0fffU;

    int cycle = 0;
    for (int addr = 0; addr < 4; ++addr) {
        const uint64_t wdata_lo = 0x0123456789abcdefULL ^ static_cast<uint64_t>(addr);
        const uint32_t wdata_hi = static_cast<uint32_t>((0xabcU ^ addr) & 0x0fffU);

        ref->RW0_addr = addr;
        wolf->RW0_addr = addr;
        ref->RW0_en = 1;
        wolf->RW0_en = 1;
        ref->RW0_wmode = 1;
        wolf->RW0_wmode = 1;
        set_w76(ref->RW0_wmask, wmask_lo, wmask_hi);
        set_w76(wolf->RW0_wmask, wmask_lo, wmask_hi);
        set_w76(ref->RW0_wdata, wdata_lo, wdata_hi);
        set_w76(wolf->RW0_wdata, wdata_lo, wdata_hi);

        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
        if (compare_step(ref, wolf, cycle++) != 0) {
            delete ref;
            delete wolf;
            return 1;
        }

        ref->RW0_wmode = 0;
        wolf->RW0_wmode = 0;
        set_w76(ref->RW0_wmask, 0, 0);
        set_w76(wolf->RW0_wmask, 0, 0);
        set_w76(ref->RW0_wdata, 0, 0);
        set_w76(wolf->RW0_wdata, 0, 0);

        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
        if (compare_step(ref, wolf, cycle++) != 0) {
            delete ref;
            delete wolf;
            return 1;
        }

        ref->RW0_en = 0;
        wolf->RW0_en = 0;
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
    VerilatedCov::write();
    return 0;
}
