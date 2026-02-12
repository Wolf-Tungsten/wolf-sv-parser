#include <cstdint>
#include <cstdio>
#include <cstring>

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

static uint64_t get_chunk(const vluint32_t *vec, int chunk) {
    const int word = chunk * 2;
    const uint64_t lo = static_cast<uint64_t>(vec[word]);
    const uint64_t hi = static_cast<uint64_t>(vec[word + 1]);
    return lo | (hi << 32);
}

static constexpr int kObsChunks = 118;
static const char *kObsNames[kObsChunks] = {
    "io_uopwriteback_valid",
    "io_uopwriteback_bits_data_0",
    "io_uopwriteback_bits_pdest",
    "io_uopwriteback_bits_pdestVl",
    "io_uopwriteback_bits_robIdx_flag",
    "io_uopwriteback_bits_robIdx_value",
    "io_uopwriteback_bits_vecWen",
    "io_uopwriteback_bits_v0Wen",
    "io_uopwriteback_bits_vlWen",
    "io_uopwriteback_bits_exceptionVec_3",
    "io_uopwriteback_bits_exceptionVec_5",
    "io_uopwriteback_bits_exceptionVec_7",
    "io_uopwriteback_bits_exceptionVec_13",
    "io_uopwriteback_bits_exceptionVec_15",
    "io_uopwriteback_bits_exceptionVec_19",
    "io_uopwriteback_bits_exceptionVec_21",
    "io_uopwriteback_bits_exceptionVec_23",
    "io_uopwriteback_bits_trigger",
    "io_uopwriteback_bits_vls_vpu_vill",
    "io_uopwriteback_bits_vls_vpu_vma",
    "io_uopwriteback_bits_vls_vpu_vta",
    "io_uopwriteback_bits_vls_vpu_vsew",
    "io_uopwriteback_bits_vls_vpu_vlmul",
    "io_uopwriteback_bits_vls_vpu_specVill",
    "io_uopwriteback_bits_vls_vpu_specVma",
    "io_uopwriteback_bits_vls_vpu_specVta",
    "io_uopwriteback_bits_vls_vpu_specVsew",
    "io_uopwriteback_bits_vls_vpu_specVlmul",
    "io_uopwriteback_bits_vls_vpu_vm",
    "io_uopwriteback_bits_vls_vpu_vstart",
    "io_uopwriteback_bits_vls_vpu_frm",
    "io_uopwriteback_bits_vls_vpu_fpu_isFpToVecInst",
    "io_uopwriteback_bits_vls_vpu_fpu_isFP32Instr",
    "io_uopwriteback_bits_vls_vpu_fpu_isFP64Instr",
    "io_uopwriteback_bits_vls_vpu_fpu_isReduction",
    "io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_2",
    "io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_4",
    "io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_8",
    "io_uopwriteback_bits_vls_vpu_vxrm",
    "io_uopwriteback_bits_vls_vpu_vuopIdx",
    "io_uopwriteback_bits_vls_vpu_lastUop",
    "io_uopwriteback_bits_vls_vpu_vmask",
    "io_uopwriteback_bits_vls_vpu_vl",
    "io_uopwriteback_bits_vls_vpu_nf",
    "io_uopwriteback_bits_vls_vpu_veew",
    "io_uopwriteback_bits_vls_vpu_isReverse",
    "io_uopwriteback_bits_vls_vpu_isExt",
    "io_uopwriteback_bits_vls_vpu_isNarrow",
    "io_uopwriteback_bits_vls_vpu_isDstMask",
    "io_uopwriteback_bits_vls_vpu_isOpMask",
    "io_uopwriteback_bits_vls_vpu_isMove",
    "io_uopwriteback_bits_vls_vpu_isDependOldVd",
    "io_uopwriteback_bits_vls_vpu_isWritePartVd",
    "io_uopwriteback_bits_vls_vpu_isVleff",
    "io_uopwriteback_bits_vls_vpu_maskVecGen",
    "io_uopwriteback_bits_vls_vpu_sew8",
    "io_uopwriteback_bits_vls_vpu_sew16",
    "io_uopwriteback_bits_vls_vpu_sew32",
    "io_uopwriteback_bits_vls_vpu_sew64",
    "io_uopwriteback_bits_vls_vdIdx",
    "io_uopwriteback_bits_vls_vdIdxInField",
    "io_uopwriteback_bits_vls_isIndexed",
    "io_uopwriteback_bits_vls_isMasked",
    "io_uopwriteback_bits_vls_isStrided",
    "io_uopwriteback_bits_vls_isWhole",
    "io_uopwriteback_bits_vls_isVecLoad",
    "io_uopwriteback_bits_vls_isVlm",
    "io_uopwriteback_bits_debug_isMMIO",
    "io_uopwriteback_bits_debug_isNCIO",
    "io_uopwriteback_bits_debug_isPerfCnt",
    "io_uopwriteback_bits_debug_paddr",
    "io_uopwriteback_bits_debug_vaddr",
    "io_uopwriteback_bits_perfDebugInfo_eliminatedMove",
    "io_uopwriteback_bits_perfDebugInfo_renameTime",
    "io_uopwriteback_bits_perfDebugInfo_dispatchTime",
    "io_uopwriteback_bits_perfDebugInfo_enqRsTime",
    "io_uopwriteback_bits_perfDebugInfo_selectTime",
    "io_uopwriteback_bits_perfDebugInfo_issueTime",
    "io_uopwriteback_bits_perfDebugInfo_runahead_checkpoint_id",
    "io_uopwriteback_bits_perfDebugInfo_tlbFirstReqTime",
    "io_uopwriteback_bits_perfDebugInfo_tlbRespTime",
    "io_uopwriteback_bits_debug_seqNum_seqNum",
    "io_uopwriteback_bits_debug_seqNum_uopIdx",
    "io_rdcache_req_valid",
    "io_rdcache_req_bits_vaddr",
    "io_rdcache_req_bits_vaddr_dup",
    "io_rdcache_s2_pc",
    "io_rdcache_is128Req",
    "io_rdcache_s1_paddr_dup_lsu",
    "io_rdcache_s1_paddr_dup_dcache",
    "io_sbuffer_valid",
    "io_sbuffer_bits_vaddr",
    "io_sbuffer_bits_data",
    "io_sbuffer_bits_mask",
    "io_sbuffer_bits_addr",
    "io_sbuffer_bits_vecValid",
    "io_vecDifftestInfo_bits_uop_fuType",
    "io_vecDifftestInfo_bits_uop_fuOpType",
    "io_vecDifftestInfo_bits_uop_vpu_nf",
    "io_vecDifftestInfo_bits_uop_vpu_veew",
    "io_vecDifftestInfo_bits_uop_robIdx_value",
    "io_dtlb_req_valid",
    "io_dtlb_req_bits_vaddr",
    "io_dtlb_req_bits_fullva",
    "io_dtlb_req_bits_cmd",
    "io_dtlb_req_bits_debug_robIdx_flag",
    "io_dtlb_req_bits_debug_robIdx_value",
    "io_flush_sbuffer_valid",
    "io_feedback_valid",
    "io_feedback_bits_sqIdx_flag",
    "io_feedback_bits_sqIdx_value",
    "io_exceptionInfo_valid",
    "io_exceptionInfo_bits_vaddr",
    "io_exceptionInfo_bits_gpaddr",
    "io_exceptionInfo_bits_isForVSnonLeafPTE",
    "dbg_state",
    "dbg_stateNext",
    "io_flush_sbuffer_empty",
};

static int compare_step(const VRef *ref, const VWolf *wolf, int cycle) {
    if (ref->obs_sig != wolf->obs_sig) {
        std::fprintf(stderr,
                     "[MISMATCH] cycle=%d obs_sig ref=0x%016llx wolf=0x%016llx\n",
                     cycle,
                     static_cast<unsigned long long>(ref->obs_sig),
                     static_cast<unsigned long long>(wolf->obs_sig));
        auto dump_named = [&](const char *name) {
            for (int i = 0; i < kObsChunks; ++i) {
                if (std::strcmp(kObsNames[i], name) == 0) {
                    const uint64_t r = get_chunk(ref->obs_vec, i);
                    const uint64_t w = get_chunk(wolf->obs_vec, i);
                    std::fprintf(stderr,
                                 "  [INFO] %s ref=0x%016llx wolf=0x%016llx\n",
                                 name,
                                 static_cast<unsigned long long>(r),
                                 static_cast<unsigned long long>(w));
                    return;
                }
            }
        };
        dump_named("dbg_state");
        dump_named("dbg_stateNext");
        dump_named("io_flush_sbuffer_empty");
        const int words = static_cast<int>(sizeof(ref->obs_vec) / sizeof(ref->obs_vec[0]));
        if (words >= kObsChunks * 2) {
            int printed = 0;
            for (int i = 0; i < kObsChunks; ++i) {
                const uint64_t r = get_chunk(ref->obs_vec, i);
                const uint64_t w = get_chunk(wolf->obs_vec, i);
                if (r != w) {
                    std::fprintf(stderr,
                                 "  [DIFF] %s ref=0x%016llx wolf=0x%016llx\n",
                                 kObsNames[i],
                                 static_cast<unsigned long long>(r),
                                 static_cast<unsigned long long>(w));
                    if (++printed >= 8) {
                        break;
                    }
                }
            }
        }
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

    const int reset_cycles = 4;
    for (int i = 0; i < reset_cycles; ++i) {
        tick(ref, wolf, 0);
        tick(ref, wolf, 1);
    }

    ref->rst_n = 1;
    wolf->rst_n = 1;

    const int max_cycles = 20000;
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        if (cycle == 200 || cycle == 210 || cycle == 215 || cycle == 216 || cycle == 217 || cycle == 218) {
            std::fprintf(stderr, "[TRACE] cycle=%d\n", cycle);
        }
        if (cycle == 217) {
            std::fprintf(stderr, "[TRACE] pre clk=0\n");
        }
        tick(ref, wolf, 0);
        if (cycle == 217) {
            std::fprintf(stderr, "[TRACE] post clk=0\n");
            std::fprintf(stderr, "[TRACE] pre clk=1\n");
        }
        tick(ref, wolf, 1);
        if (cycle == 217) {
            std::fprintf(stderr, "[TRACE] post clk=1\n");
        }

        if (cycle == 217) {
            std::fprintf(stderr, "[TRACE] pre compare\n");
        }
        if (compare_step(ref, wolf, cycle) != 0) {
            delete ref;
            delete wolf;
            return 1;
        }
        if (cycle == 217) {
            std::fprintf(stderr, "[TRACE] post compare\n");
        }

        if (Verilated::gotFinish()) {
            break;
        }
    }

    VerilatedCov::write();
    delete ref;
    delete wolf;
    return 0;
}
