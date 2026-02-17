#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

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

struct Event {
    int time = 0;
    int signal_id = 0;
    std::string value;
};

struct SignalMeta {
    int width;
    int words;
};

template <typename T>
static inline std::remove_reference_t<T> as_scalar(uint64_t value) {
    return static_cast<std::remove_reference_t<T>>(value);
}

static const SignalMeta kSignals[] = {
    {1, 1}, // clock
    {1, 1}, // reset
    {1, 1}, // io_ctrl_ubtbEnable
    {1, 1}, // io_ctrl_abtbEnable
    {1, 1}, // io_ctrl_mbtbEnable
    {1, 1}, // io_ctrl_tageEnable
    {1, 1}, // io_ctrl_scEnable
    {1, 1}, // io_ctrl_ittageEnable
    {47, 2}, // io_resetVector_addr
    {1, 1}, // io_fromFtq_redirect_valid
    {49, 2}, // io_fromFtq_redirect_bits_cfiPc_addr
    {49, 2}, // io_fromFtq_redirect_bits_target_addr
    {1, 1}, // io_fromFtq_redirect_bits_taken
    {2, 1}, // io_fromFtq_redirect_bits_attribute_branchType
    {2, 1}, // io_fromFtq_redirect_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_redirect_bits_meta_phr_phrPtr_flag
    {10, 1}, // io_fromFtq_redirect_bits_meta_phr_phrPtr_value
    {13, 1}, // io_fromFtq_redirect_bits_meta_phr_phrLowBits
    {16, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_ghr
    {8, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_bw
    {1, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_0
    {1, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_1
    {1, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_2
    {1, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_3
    {1, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_4
    {1, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_5
    {1, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_6
    {1, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_7
    {2, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_0_branchType
    {2, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_1_branchType
    {2, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_2_branchType
    {2, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_3_branchType
    {2, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_4_branchType
    {2, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_5_branchType
    {2, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_6_branchType
    {2, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_7_branchType
    {5, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_position_0
    {5, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_position_1
    {5, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_position_2
    {5, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_position_3
    {5, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_position_4
    {5, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_position_5
    {5, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_position_6
    {5, 1}, // io_fromFtq_redirect_bits_meta_commonHRMeta_position_7
    {4, 1}, // io_fromFtq_redirect_bits_meta_ras_ssp
    {3, 1}, // io_fromFtq_redirect_bits_meta_ras_sctr
    {1, 1}, // io_fromFtq_redirect_bits_meta_ras_tosw_flag
    {5, 1}, // io_fromFtq_redirect_bits_meta_ras_tosw_value
    {1, 1}, // io_fromFtq_redirect_bits_meta_ras_tosr_flag
    {5, 1}, // io_fromFtq_redirect_bits_meta_ras_tosr_value
    {1, 1}, // io_fromFtq_redirect_bits_meta_ras_nos_flag
    {5, 1}, // io_fromFtq_redirect_bits_meta_ras_nos_value
    {1, 1}, // io_fromFtq_train_valid
    {49, 2}, // io_fromFtq_train_bits_startPc_addr
    {1, 1}, // io_fromFtq_train_bits_branches_0_valid
    {49, 2}, // io_fromFtq_train_bits_branches_0_bits_target_addr
    {1, 1}, // io_fromFtq_train_bits_branches_0_bits_taken
    {5, 1}, // io_fromFtq_train_bits_branches_0_bits_cfiPosition
    {2, 1}, // io_fromFtq_train_bits_branches_0_bits_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_branches_0_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_train_bits_branches_0_bits_mispredict
    {1, 1}, // io_fromFtq_train_bits_branches_1_valid
    {49, 2}, // io_fromFtq_train_bits_branches_1_bits_target_addr
    {1, 1}, // io_fromFtq_train_bits_branches_1_bits_taken
    {5, 1}, // io_fromFtq_train_bits_branches_1_bits_cfiPosition
    {2, 1}, // io_fromFtq_train_bits_branches_1_bits_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_branches_1_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_train_bits_branches_1_bits_mispredict
    {1, 1}, // io_fromFtq_train_bits_branches_2_valid
    {49, 2}, // io_fromFtq_train_bits_branches_2_bits_target_addr
    {1, 1}, // io_fromFtq_train_bits_branches_2_bits_taken
    {5, 1}, // io_fromFtq_train_bits_branches_2_bits_cfiPosition
    {2, 1}, // io_fromFtq_train_bits_branches_2_bits_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_branches_2_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_train_bits_branches_2_bits_mispredict
    {1, 1}, // io_fromFtq_train_bits_branches_3_valid
    {49, 2}, // io_fromFtq_train_bits_branches_3_bits_target_addr
    {1, 1}, // io_fromFtq_train_bits_branches_3_bits_taken
    {5, 1}, // io_fromFtq_train_bits_branches_3_bits_cfiPosition
    {2, 1}, // io_fromFtq_train_bits_branches_3_bits_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_branches_3_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_train_bits_branches_3_bits_mispredict
    {1, 1}, // io_fromFtq_train_bits_branches_4_valid
    {49, 2}, // io_fromFtq_train_bits_branches_4_bits_target_addr
    {1, 1}, // io_fromFtq_train_bits_branches_4_bits_taken
    {5, 1}, // io_fromFtq_train_bits_branches_4_bits_cfiPosition
    {2, 1}, // io_fromFtq_train_bits_branches_4_bits_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_branches_4_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_train_bits_branches_4_bits_mispredict
    {1, 1}, // io_fromFtq_train_bits_branches_5_valid
    {49, 2}, // io_fromFtq_train_bits_branches_5_bits_target_addr
    {1, 1}, // io_fromFtq_train_bits_branches_5_bits_taken
    {5, 1}, // io_fromFtq_train_bits_branches_5_bits_cfiPosition
    {2, 1}, // io_fromFtq_train_bits_branches_5_bits_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_branches_5_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_train_bits_branches_5_bits_mispredict
    {1, 1}, // io_fromFtq_train_bits_branches_6_valid
    {49, 2}, // io_fromFtq_train_bits_branches_6_bits_target_addr
    {1, 1}, // io_fromFtq_train_bits_branches_6_bits_taken
    {5, 1}, // io_fromFtq_train_bits_branches_6_bits_cfiPosition
    {2, 1}, // io_fromFtq_train_bits_branches_6_bits_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_branches_6_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_train_bits_branches_6_bits_mispredict
    {1, 1}, // io_fromFtq_train_bits_branches_7_valid
    {49, 2}, // io_fromFtq_train_bits_branches_7_bits_target_addr
    {1, 1}, // io_fromFtq_train_bits_branches_7_bits_taken
    {5, 1}, // io_fromFtq_train_bits_branches_7_bits_cfiPosition
    {2, 1}, // io_fromFtq_train_bits_branches_7_bits_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_branches_7_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_train_bits_branches_7_bits_mispredict
    {1, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_0_rawHit
    {5, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_0_position
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_rasAction
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_0_counter_value
    {1, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_1_rawHit
    {5, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_1_position
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_rasAction
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_1_counter_value
    {1, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_2_rawHit
    {5, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_2_position
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_rasAction
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_2_counter_value
    {1, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_3_rawHit
    {5, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_3_position
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_rasAction
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_0_3_counter_value
    {1, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_0_rawHit
    {5, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_0_position
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_rasAction
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_0_counter_value
    {1, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_1_rawHit
    {5, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_1_position
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_rasAction
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_1_counter_value
    {1, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_2_rawHit
    {5, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_2_position
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_rasAction
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_2_counter_value
    {1, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_3_rawHit
    {5, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_3_position
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_branchType
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_rasAction
    {2, 1}, // io_fromFtq_train_bits_meta_mbtb_entries_1_3_counter_value
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_0_useProvider
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_0_providerTableIdx
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_0_providerWayIdx
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_0_providerTakenCtr_value
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_0_providerUsefulCtr_value
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_0_altOrBasePred
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_1_useProvider
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_1_providerTableIdx
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_1_providerWayIdx
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_1_providerTakenCtr_value
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_1_providerUsefulCtr_value
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_1_altOrBasePred
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_2_useProvider
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_2_providerTableIdx
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_2_providerWayIdx
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_2_providerTakenCtr_value
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_2_providerUsefulCtr_value
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_2_altOrBasePred
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_3_useProvider
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_3_providerTableIdx
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_3_providerWayIdx
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_3_providerTakenCtr_value
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_3_providerUsefulCtr_value
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_3_altOrBasePred
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_4_useProvider
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_4_providerTableIdx
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_4_providerWayIdx
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_4_providerTakenCtr_value
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_4_providerUsefulCtr_value
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_4_altOrBasePred
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_5_useProvider
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_5_providerTableIdx
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_5_providerWayIdx
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_5_providerTakenCtr_value
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_5_providerUsefulCtr_value
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_5_altOrBasePred
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_6_useProvider
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_6_providerTableIdx
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_6_providerWayIdx
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_6_providerTakenCtr_value
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_6_providerUsefulCtr_value
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_6_altOrBasePred
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_7_useProvider
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_7_providerTableIdx
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_7_providerWayIdx
    {3, 1}, // io_fromFtq_train_bits_meta_tage_entries_7_providerTakenCtr_value
    {2, 1}, // io_fromFtq_train_bits_meta_tage_entries_7_providerUsefulCtr_value
    {1, 1}, // io_fromFtq_train_bits_meta_tage_entries_7_altOrBasePred
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_0_0
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_0_1
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_0_2
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_0_3
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_0_4
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_0_5
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_0_6
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_0_7
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_1_0
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_1_1
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_1_2
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_1_3
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_1_4
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_1_5
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_1_6
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scPathResp_1_7
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_0
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_1
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_2
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_3
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_4
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_5
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_6
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_7
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_8
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_9
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_10
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_11
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_12
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_13
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_14
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_15
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_16
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_17
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_18
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_19
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_20
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_21
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_22
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_23
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_24
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_25
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_26
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_27
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_28
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_29
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_30
    {6, 1}, // io_fromFtq_train_bits_meta_sc_scBiasResp_31
    {2, 1}, // io_fromFtq_train_bits_meta_sc_scBiasLowerBits_0
    {2, 1}, // io_fromFtq_train_bits_meta_sc_scBiasLowerBits_1
    {2, 1}, // io_fromFtq_train_bits_meta_sc_scBiasLowerBits_2
    {2, 1}, // io_fromFtq_train_bits_meta_sc_scBiasLowerBits_3
    {2, 1}, // io_fromFtq_train_bits_meta_sc_scBiasLowerBits_4
    {2, 1}, // io_fromFtq_train_bits_meta_sc_scBiasLowerBits_5
    {2, 1}, // io_fromFtq_train_bits_meta_sc_scBiasLowerBits_6
    {2, 1}, // io_fromFtq_train_bits_meta_sc_scBiasLowerBits_7
    {1, 1}, // io_fromFtq_train_bits_meta_sc_scCommonHR_valid
    {16, 1}, // io_fromFtq_train_bits_meta_sc_scCommonHR_ghr
    {8, 1}, // io_fromFtq_train_bits_meta_sc_scCommonHR_bw
    {1, 1}, // io_fromFtq_train_bits_meta_sc_scPred_0
    {1, 1}, // io_fromFtq_train_bits_meta_sc_scPred_1
    {1, 1}, // io_fromFtq_train_bits_meta_sc_scPred_2
    {1, 1}, // io_fromFtq_train_bits_meta_sc_scPred_3
    {1, 1}, // io_fromFtq_train_bits_meta_sc_scPred_4
    {1, 1}, // io_fromFtq_train_bits_meta_sc_scPred_5
    {1, 1}, // io_fromFtq_train_bits_meta_sc_scPred_6
    {1, 1}, // io_fromFtq_train_bits_meta_sc_scPred_7
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePred_0
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePred_1
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePred_2
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePred_3
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePred_4
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePred_5
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePred_6
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePred_7
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePredValid_0
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePredValid_1
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePredValid_2
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePredValid_3
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePredValid_4
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePredValid_5
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePredValid_6
    {1, 1}, // io_fromFtq_train_bits_meta_sc_tagePredValid_7
    {1, 1}, // io_fromFtq_train_bits_meta_sc_useScPred_0
    {1, 1}, // io_fromFtq_train_bits_meta_sc_useScPred_1
    {1, 1}, // io_fromFtq_train_bits_meta_sc_useScPred_2
    {1, 1}, // io_fromFtq_train_bits_meta_sc_useScPred_3
    {1, 1}, // io_fromFtq_train_bits_meta_sc_useScPred_4
    {1, 1}, // io_fromFtq_train_bits_meta_sc_useScPred_5
    {1, 1}, // io_fromFtq_train_bits_meta_sc_useScPred_6
    {1, 1}, // io_fromFtq_train_bits_meta_sc_useScPred_7
    {1, 1}, // io_fromFtq_train_bits_meta_sc_sumAboveThres_0
    {1, 1}, // io_fromFtq_train_bits_meta_sc_sumAboveThres_1
    {1, 1}, // io_fromFtq_train_bits_meta_sc_sumAboveThres_2
    {1, 1}, // io_fromFtq_train_bits_meta_sc_sumAboveThres_3
    {1, 1}, // io_fromFtq_train_bits_meta_sc_sumAboveThres_4
    {1, 1}, // io_fromFtq_train_bits_meta_sc_sumAboveThres_5
    {1, 1}, // io_fromFtq_train_bits_meta_sc_sumAboveThres_6
    {1, 1}, // io_fromFtq_train_bits_meta_sc_sumAboveThres_7
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_0
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_1
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_2
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_3
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_4
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_5
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_6
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_7
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_0
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_1
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_2
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_3
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_4
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_5
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_6
    {1, 1}, // io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_7
    {7, 1}, // io_fromFtq_train_bits_meta_sc_debug_predPathIdx_0
    {7, 1}, // io_fromFtq_train_bits_meta_sc_debug_predPathIdx_1
    {7, 1}, // io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_0
    {7, 1}, // io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_1
    {7, 1}, // io_fromFtq_train_bits_meta_sc_debug_predBWIdx_0
    {7, 1}, // io_fromFtq_train_bits_meta_sc_debug_predBWIdx_1
    {7, 1}, // io_fromFtq_train_bits_meta_sc_debug_predBiasIdx
    {1, 1}, // io_fromFtq_train_bits_meta_ittage_provider_valid
    {3, 1}, // io_fromFtq_train_bits_meta_ittage_provider_bits
    {1, 1}, // io_fromFtq_train_bits_meta_ittage_altProvider_valid
    {3, 1}, // io_fromFtq_train_bits_meta_ittage_altProvider_bits
    {1, 1}, // io_fromFtq_train_bits_meta_ittage_altDiffers
    {1, 1}, // io_fromFtq_train_bits_meta_ittage_providerUsefulCnt_value
    {2, 1}, // io_fromFtq_train_bits_meta_ittage_providerCnt_value
    {2, 1}, // io_fromFtq_train_bits_meta_ittage_altProviderCnt_value
    {1, 1}, // io_fromFtq_train_bits_meta_ittage_allocate_valid
    {3, 1}, // io_fromFtq_train_bits_meta_ittage_allocate_bits
    {49, 2}, // io_fromFtq_train_bits_meta_ittage_providerTarget_addr
    {49, 2}, // io_fromFtq_train_bits_meta_ittage_altProviderTarget_addr
    {10, 1}, // io_fromFtq_train_bits_meta_phr_phrPtr_value
    {13, 1}, // io_fromFtq_train_bits_meta_phr_phrLowBits
    {13, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist
    {12, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist
    {13, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist
    {12, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist
    {13, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist
    {12, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist
    {13, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist
    {12, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist
    {8, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist
    {13, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist
    {12, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist
    {13, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist
    {12, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist
    {12, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist
    {11, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist
    {8, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist
    {7, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist
    {8, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist
    {9, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist
    {8, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist
    {8, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist
    {7, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist
    {4, 1}, // io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist
    {1, 1}, // io_fromFtq_commit_valid
    {4, 1}, // io_fromFtq_commit_bits_meta_ras_ssp
    {1, 1}, // io_fromFtq_commit_bits_meta_ras_tosw_flag
    {5, 1}, // io_fromFtq_commit_bits_meta_ras_tosw_value
    {2, 1}, // io_fromFtq_commit_bits_attribute_rasAction
    {1, 1}, // io_fromFtq_bpuPtr_flag
    {6, 1}, // io_fromFtq_bpuPtr_value
    {1, 1}, // io_toFtq_prediction_ready
    {8, 1}, // boreChildrenBd_bore_array
    {1, 1}, // boreChildrenBd_bore_all
    {1, 1}, // boreChildrenBd_bore_req
    {1, 1}, // boreChildrenBd_bore_writeen
    {38, 2}, // boreChildrenBd_bore_be
    {10, 1}, // boreChildrenBd_bore_addr
    {112, 4}, // boreChildrenBd_bore_indata
    {1, 1}, // boreChildrenBd_bore_readen
    {10, 1}, // boreChildrenBd_bore_addr_rd
    {8, 1}, // boreChildrenBd_bore_1_array
    {1, 1}, // boreChildrenBd_bore_1_all
    {1, 1}, // boreChildrenBd_bore_1_req
    {1, 1}, // boreChildrenBd_bore_1_writeen
    {38, 2}, // boreChildrenBd_bore_1_be
    {8, 1}, // boreChildrenBd_bore_1_addr
    {38, 2}, // boreChildrenBd_bore_1_indata
    {1, 1}, // boreChildrenBd_bore_1_readen
    {8, 1}, // boreChildrenBd_bore_1_addr_rd
    {8, 1}, // boreChildrenBd_bore_2_array
    {1, 1}, // boreChildrenBd_bore_2_all
    {1, 1}, // boreChildrenBd_bore_2_req
    {1, 1}, // boreChildrenBd_bore_2_writeen
    {76, 3}, // boreChildrenBd_bore_2_be
    {8, 1}, // boreChildrenBd_bore_2_addr
    {76, 3}, // boreChildrenBd_bore_2_indata
    {1, 1}, // boreChildrenBd_bore_2_readen
    {8, 1}, // boreChildrenBd_bore_2_addr_rd
    {8, 1}, // boreChildrenBd_bore_3_array
    {1, 1}, // boreChildrenBd_bore_3_all
    {1, 1}, // boreChildrenBd_bore_3_req
    {1, 1}, // boreChildrenBd_bore_3_writeen
    {76, 3}, // boreChildrenBd_bore_3_be
    {8, 1}, // boreChildrenBd_bore_3_addr
    {76, 3}, // boreChildrenBd_bore_3_indata
    {1, 1}, // boreChildrenBd_bore_3_readen
    {8, 1}, // boreChildrenBd_bore_3_addr_rd
    {8, 1}, // boreChildrenBd_bore_4_array
    {1, 1}, // boreChildrenBd_bore_4_all
    {1, 1}, // boreChildrenBd_bore_4_req
    {1, 1}, // boreChildrenBd_bore_4_writeen
    {76, 3}, // boreChildrenBd_bore_4_be
    {8, 1}, // boreChildrenBd_bore_4_addr
    {76, 3}, // boreChildrenBd_bore_4_indata
    {1, 1}, // boreChildrenBd_bore_4_readen
    {8, 1}, // boreChildrenBd_bore_4_addr_rd
    {8, 1}, // boreChildrenBd_bore_5_addr
    {8, 1}, // boreChildrenBd_bore_5_addr_rd
    {48, 2}, // boreChildrenBd_bore_5_wdata
    {8, 1}, // boreChildrenBd_bore_5_wmask
    {1, 1}, // boreChildrenBd_bore_5_re
    {1, 1}, // boreChildrenBd_bore_5_we
    {1, 1}, // boreChildrenBd_bore_5_ack
    {1, 1}, // boreChildrenBd_bore_5_selectedOH
    {8, 1}, // boreChildrenBd_bore_5_array
    {8, 1}, // boreChildrenBd_bore_6_addr
    {8, 1}, // boreChildrenBd_bore_6_addr_rd
    {48, 2}, // boreChildrenBd_bore_6_wdata
    {8, 1}, // boreChildrenBd_bore_6_wmask
    {1, 1}, // boreChildrenBd_bore_6_re
    {1, 1}, // boreChildrenBd_bore_6_we
    {1, 1}, // boreChildrenBd_bore_6_ack
    {1, 1}, // boreChildrenBd_bore_6_selectedOH
    {8, 1}, // boreChildrenBd_bore_6_array
    {8, 1}, // boreChildrenBd_bore_7_addr
    {8, 1}, // boreChildrenBd_bore_7_addr_rd
    {48, 2}, // boreChildrenBd_bore_7_wdata
    {8, 1}, // boreChildrenBd_bore_7_wmask
    {1, 1}, // boreChildrenBd_bore_7_re
    {1, 1}, // boreChildrenBd_bore_7_we
    {1, 1}, // boreChildrenBd_bore_7_ack
    {1, 1}, // boreChildrenBd_bore_7_selectedOH
    {8, 1}, // boreChildrenBd_bore_7_array
    {8, 1}, // boreChildrenBd_bore_8_addr
    {8, 1}, // boreChildrenBd_bore_8_addr_rd
    {48, 2}, // boreChildrenBd_bore_8_wdata
    {8, 1}, // boreChildrenBd_bore_8_wmask
    {1, 1}, // boreChildrenBd_bore_8_re
    {1, 1}, // boreChildrenBd_bore_8_we
    {1, 1}, // boreChildrenBd_bore_8_ack
    {1, 1}, // boreChildrenBd_bore_8_selectedOH
    {8, 1}, // boreChildrenBd_bore_8_array
    {8, 1}, // boreChildrenBd_bore_9_addr
    {8, 1}, // boreChildrenBd_bore_9_addr_rd
    {48, 2}, // boreChildrenBd_bore_9_wdata
    {8, 1}, // boreChildrenBd_bore_9_wmask
    {1, 1}, // boreChildrenBd_bore_9_re
    {1, 1}, // boreChildrenBd_bore_9_we
    {1, 1}, // boreChildrenBd_bore_9_ack
    {1, 1}, // boreChildrenBd_bore_9_selectedOH
    {8, 1}, // boreChildrenBd_bore_9_array
    {8, 1}, // boreChildrenBd_bore_10_addr
    {8, 1}, // boreChildrenBd_bore_10_addr_rd
    {48, 2}, // boreChildrenBd_bore_10_wdata
    {8, 1}, // boreChildrenBd_bore_10_wmask
    {1, 1}, // boreChildrenBd_bore_10_re
    {1, 1}, // boreChildrenBd_bore_10_we
    {1, 1}, // boreChildrenBd_bore_10_ack
    {1, 1}, // boreChildrenBd_bore_10_selectedOH
    {8, 1}, // boreChildrenBd_bore_10_array
    {8, 1}, // boreChildrenBd_bore_11_addr
    {8, 1}, // boreChildrenBd_bore_11_addr_rd
    {48, 2}, // boreChildrenBd_bore_11_wdata
    {8, 1}, // boreChildrenBd_bore_11_wmask
    {1, 1}, // boreChildrenBd_bore_11_re
    {1, 1}, // boreChildrenBd_bore_11_we
    {1, 1}, // boreChildrenBd_bore_11_ack
    {1, 1}, // boreChildrenBd_bore_11_selectedOH
    {8, 1}, // boreChildrenBd_bore_11_array
    {8, 1}, // boreChildrenBd_bore_12_addr
    {8, 1}, // boreChildrenBd_bore_12_addr_rd
    {48, 2}, // boreChildrenBd_bore_12_wdata
    {8, 1}, // boreChildrenBd_bore_12_wmask
    {1, 1}, // boreChildrenBd_bore_12_re
    {1, 1}, // boreChildrenBd_bore_12_we
    {1, 1}, // boreChildrenBd_bore_12_ack
    {1, 1}, // boreChildrenBd_bore_12_selectedOH
    {8, 1}, // boreChildrenBd_bore_12_array
    {8, 1}, // boreChildrenBd_bore_13_addr
    {8, 1}, // boreChildrenBd_bore_13_addr_rd
    {48, 2}, // boreChildrenBd_bore_13_wdata
    {8, 1}, // boreChildrenBd_bore_13_wmask
    {1, 1}, // boreChildrenBd_bore_13_re
    {1, 1}, // boreChildrenBd_bore_13_we
    {1, 1}, // boreChildrenBd_bore_13_ack
    {1, 1}, // boreChildrenBd_bore_13_selectedOH
    {8, 1}, // boreChildrenBd_bore_13_array
    {8, 1}, // boreChildrenBd_bore_14_addr
    {8, 1}, // boreChildrenBd_bore_14_addr_rd
    {48, 2}, // boreChildrenBd_bore_14_wdata
    {8, 1}, // boreChildrenBd_bore_14_wmask
    {1, 1}, // boreChildrenBd_bore_14_re
    {1, 1}, // boreChildrenBd_bore_14_we
    {1, 1}, // boreChildrenBd_bore_14_ack
    {1, 1}, // boreChildrenBd_bore_14_selectedOH
    {8, 1}, // boreChildrenBd_bore_14_array
    {8, 1}, // boreChildrenBd_bore_15_addr
    {8, 1}, // boreChildrenBd_bore_15_addr_rd
    {48, 2}, // boreChildrenBd_bore_15_wdata
    {8, 1}, // boreChildrenBd_bore_15_wmask
    {1, 1}, // boreChildrenBd_bore_15_re
    {1, 1}, // boreChildrenBd_bore_15_we
    {1, 1}, // boreChildrenBd_bore_15_ack
    {1, 1}, // boreChildrenBd_bore_15_selectedOH
    {8, 1}, // boreChildrenBd_bore_15_array
    {8, 1}, // boreChildrenBd_bore_16_addr
    {8, 1}, // boreChildrenBd_bore_16_addr_rd
    {48, 2}, // boreChildrenBd_bore_16_wdata
    {8, 1}, // boreChildrenBd_bore_16_wmask
    {1, 1}, // boreChildrenBd_bore_16_re
    {1, 1}, // boreChildrenBd_bore_16_we
    {1, 1}, // boreChildrenBd_bore_16_ack
    {1, 1}, // boreChildrenBd_bore_16_selectedOH
    {8, 1}, // boreChildrenBd_bore_16_array
    {8, 1}, // boreChildrenBd_bore_17_addr
    {8, 1}, // boreChildrenBd_bore_17_addr_rd
    {192, 6}, // boreChildrenBd_bore_17_wdata
    {32, 1}, // boreChildrenBd_bore_17_wmask
    {1, 1}, // boreChildrenBd_bore_17_re
    {1, 1}, // boreChildrenBd_bore_17_we
    {1, 1}, // boreChildrenBd_bore_17_ack
    {1, 1}, // boreChildrenBd_bore_17_selectedOH
    {8, 1}, // boreChildrenBd_bore_17_array
    {8, 1}, // boreChildrenBd_bore_18_addr
    {8, 1}, // boreChildrenBd_bore_18_addr_rd
    {192, 6}, // boreChildrenBd_bore_18_wdata
    {32, 1}, // boreChildrenBd_bore_18_wmask
    {1, 1}, // boreChildrenBd_bore_18_re
    {1, 1}, // boreChildrenBd_bore_18_we
    {1, 1}, // boreChildrenBd_bore_18_ack
    {1, 1}, // boreChildrenBd_bore_18_selectedOH
    {8, 1}, // boreChildrenBd_bore_18_array
    {1, 1}, // sigFromSrams_bore_ram_hold
    {1, 1}, // sigFromSrams_bore_ram_bypass
    {1, 1}, // sigFromSrams_bore_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_cgen
    {1, 1}, // sigFromSrams_bore_1_ram_hold
    {1, 1}, // sigFromSrams_bore_1_ram_bypass
    {1, 1}, // sigFromSrams_bore_1_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_1_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_1_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_1_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_1_cgen
    {1, 1}, // sigFromSrams_bore_2_ram_hold
    {1, 1}, // sigFromSrams_bore_2_ram_bypass
    {1, 1}, // sigFromSrams_bore_2_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_2_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_2_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_2_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_2_cgen
    {1, 1}, // sigFromSrams_bore_3_ram_hold
    {1, 1}, // sigFromSrams_bore_3_ram_bypass
    {1, 1}, // sigFromSrams_bore_3_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_3_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_3_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_3_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_3_cgen
    {1, 1}, // sigFromSrams_bore_4_ram_hold
    {1, 1}, // sigFromSrams_bore_4_ram_bypass
    {1, 1}, // sigFromSrams_bore_4_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_4_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_4_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_4_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_4_cgen
    {1, 1}, // sigFromSrams_bore_5_ram_hold
    {1, 1}, // sigFromSrams_bore_5_ram_bypass
    {1, 1}, // sigFromSrams_bore_5_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_5_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_5_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_5_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_5_cgen
    {1, 1}, // sigFromSrams_bore_6_ram_hold
    {1, 1}, // sigFromSrams_bore_6_ram_bypass
    {1, 1}, // sigFromSrams_bore_6_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_6_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_6_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_6_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_6_cgen
    {1, 1}, // sigFromSrams_bore_7_ram_hold
    {1, 1}, // sigFromSrams_bore_7_ram_bypass
    {1, 1}, // sigFromSrams_bore_7_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_7_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_7_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_7_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_7_cgen
    {1, 1}, // sigFromSrams_bore_8_ram_hold
    {1, 1}, // sigFromSrams_bore_8_ram_bypass
    {1, 1}, // sigFromSrams_bore_8_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_8_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_8_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_8_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_8_cgen
    {1, 1}, // sigFromSrams_bore_9_ram_hold
    {1, 1}, // sigFromSrams_bore_9_ram_bypass
    {1, 1}, // sigFromSrams_bore_9_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_9_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_9_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_9_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_9_cgen
    {1, 1}, // sigFromSrams_bore_10_ram_hold
    {1, 1}, // sigFromSrams_bore_10_ram_bypass
    {1, 1}, // sigFromSrams_bore_10_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_10_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_10_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_10_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_10_cgen
    {1, 1}, // sigFromSrams_bore_11_ram_hold
    {1, 1}, // sigFromSrams_bore_11_ram_bypass
    {1, 1}, // sigFromSrams_bore_11_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_11_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_11_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_11_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_11_cgen
    {1, 1}, // sigFromSrams_bore_12_ram_hold
    {1, 1}, // sigFromSrams_bore_12_ram_bypass
    {1, 1}, // sigFromSrams_bore_12_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_12_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_12_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_12_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_12_cgen
    {1, 1}, // sigFromSrams_bore_13_ram_hold
    {1, 1}, // sigFromSrams_bore_13_ram_bypass
    {1, 1}, // sigFromSrams_bore_13_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_13_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_13_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_13_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_13_cgen
    {1, 1}, // sigFromSrams_bore_14_ram_hold
    {1, 1}, // sigFromSrams_bore_14_ram_bypass
    {1, 1}, // sigFromSrams_bore_14_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_14_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_14_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_14_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_14_cgen
    {1, 1}, // sigFromSrams_bore_15_ram_hold
    {1, 1}, // sigFromSrams_bore_15_ram_bypass
    {1, 1}, // sigFromSrams_bore_15_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_15_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_15_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_15_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_15_cgen
    {1, 1}, // sigFromSrams_bore_16_ram_hold
    {1, 1}, // sigFromSrams_bore_16_ram_bypass
    {1, 1}, // sigFromSrams_bore_16_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_16_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_16_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_16_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_16_cgen
    {1, 1}, // sigFromSrams_bore_17_ram_hold
    {1, 1}, // sigFromSrams_bore_17_ram_bypass
    {1, 1}, // sigFromSrams_bore_17_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_17_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_17_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_17_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_17_cgen
    {1, 1}, // sigFromSrams_bore_18_ram_hold
    {1, 1}, // sigFromSrams_bore_18_ram_bypass
    {1, 1}, // sigFromSrams_bore_18_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_18_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_18_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_18_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_18_cgen
    {1, 1}, // sigFromSrams_bore_19_ram_hold
    {1, 1}, // sigFromSrams_bore_19_ram_bypass
    {1, 1}, // sigFromSrams_bore_19_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_19_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_19_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_19_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_19_cgen
    {1, 1}, // sigFromSrams_bore_20_ram_hold
    {1, 1}, // sigFromSrams_bore_20_ram_bypass
    {1, 1}, // sigFromSrams_bore_20_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_20_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_20_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_20_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_20_cgen
    {1, 1}, // sigFromSrams_bore_21_ram_hold
    {1, 1}, // sigFromSrams_bore_21_ram_bypass
    {1, 1}, // sigFromSrams_bore_21_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_21_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_21_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_21_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_21_cgen
    {1, 1}, // sigFromSrams_bore_22_ram_hold
    {1, 1}, // sigFromSrams_bore_22_ram_bypass
    {1, 1}, // sigFromSrams_bore_22_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_22_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_22_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_22_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_22_cgen
    {1, 1}, // sigFromSrams_bore_23_ram_hold
    {1, 1}, // sigFromSrams_bore_23_ram_bypass
    {1, 1}, // sigFromSrams_bore_23_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_23_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_23_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_23_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_23_cgen
    {1, 1}, // sigFromSrams_bore_24_ram_hold
    {1, 1}, // sigFromSrams_bore_24_ram_bypass
    {1, 1}, // sigFromSrams_bore_24_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_24_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_24_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_24_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_24_cgen
    {1, 1}, // sigFromSrams_bore_25_ram_hold
    {1, 1}, // sigFromSrams_bore_25_ram_bypass
    {1, 1}, // sigFromSrams_bore_25_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_25_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_25_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_25_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_25_cgen
    {1, 1}, // sigFromSrams_bore_26_ram_hold
    {1, 1}, // sigFromSrams_bore_26_ram_bypass
    {1, 1}, // sigFromSrams_bore_26_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_26_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_26_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_26_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_26_cgen
    {1, 1}, // sigFromSrams_bore_27_ram_hold
    {1, 1}, // sigFromSrams_bore_27_ram_bypass
    {1, 1}, // sigFromSrams_bore_27_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_27_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_27_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_27_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_27_cgen
    {1, 1}, // sigFromSrams_bore_28_ram_hold
    {1, 1}, // sigFromSrams_bore_28_ram_bypass
    {1, 1}, // sigFromSrams_bore_28_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_28_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_28_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_28_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_28_cgen
    {1, 1}, // sigFromSrams_bore_29_ram_hold
    {1, 1}, // sigFromSrams_bore_29_ram_bypass
    {1, 1}, // sigFromSrams_bore_29_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_29_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_29_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_29_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_29_cgen
    {1, 1}, // sigFromSrams_bore_30_ram_hold
    {1, 1}, // sigFromSrams_bore_30_ram_bypass
    {1, 1}, // sigFromSrams_bore_30_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_30_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_30_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_30_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_30_cgen
    {1, 1}, // sigFromSrams_bore_31_ram_hold
    {1, 1}, // sigFromSrams_bore_31_ram_bypass
    {1, 1}, // sigFromSrams_bore_31_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_31_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_31_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_31_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_31_cgen
    {1, 1}, // sigFromSrams_bore_32_ram_hold
    {1, 1}, // sigFromSrams_bore_32_ram_bypass
    {1, 1}, // sigFromSrams_bore_32_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_32_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_32_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_32_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_32_cgen
    {1, 1}, // sigFromSrams_bore_33_ram_hold
    {1, 1}, // sigFromSrams_bore_33_ram_bypass
    {1, 1}, // sigFromSrams_bore_33_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_33_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_33_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_33_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_33_cgen
    {1, 1}, // sigFromSrams_bore_34_ram_hold
    {1, 1}, // sigFromSrams_bore_34_ram_bypass
    {1, 1}, // sigFromSrams_bore_34_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_34_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_34_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_34_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_34_cgen
    {1, 1}, // sigFromSrams_bore_35_ram_hold
    {1, 1}, // sigFromSrams_bore_35_ram_bypass
    {1, 1}, // sigFromSrams_bore_35_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_35_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_35_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_35_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_35_cgen
    {1, 1}, // sigFromSrams_bore_36_ram_hold
    {1, 1}, // sigFromSrams_bore_36_ram_bypass
    {1, 1}, // sigFromSrams_bore_36_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_36_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_36_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_36_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_36_cgen
    {1, 1}, // sigFromSrams_bore_37_ram_hold
    {1, 1}, // sigFromSrams_bore_37_ram_bypass
    {1, 1}, // sigFromSrams_bore_37_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_37_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_37_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_37_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_37_cgen
    {1, 1}, // sigFromSrams_bore_38_ram_hold
    {1, 1}, // sigFromSrams_bore_38_ram_bypass
    {1, 1}, // sigFromSrams_bore_38_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_38_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_38_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_38_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_38_cgen
    {1, 1}, // sigFromSrams_bore_39_ram_hold
    {1, 1}, // sigFromSrams_bore_39_ram_bypass
    {1, 1}, // sigFromSrams_bore_39_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_39_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_39_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_39_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_39_cgen
    {1, 1}, // sigFromSrams_bore_40_ram_hold
    {1, 1}, // sigFromSrams_bore_40_ram_bypass
    {1, 1}, // sigFromSrams_bore_40_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_40_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_40_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_40_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_40_cgen
    {1, 1}, // sigFromSrams_bore_41_ram_hold
    {1, 1}, // sigFromSrams_bore_41_ram_bypass
    {1, 1}, // sigFromSrams_bore_41_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_41_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_41_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_41_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_41_cgen
    {1, 1}, // sigFromSrams_bore_42_ram_hold
    {1, 1}, // sigFromSrams_bore_42_ram_bypass
    {1, 1}, // sigFromSrams_bore_42_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_42_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_42_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_42_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_42_cgen
    {1, 1}, // sigFromSrams_bore_43_ram_hold
    {1, 1}, // sigFromSrams_bore_43_ram_bypass
    {1, 1}, // sigFromSrams_bore_43_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_43_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_43_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_43_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_43_cgen
    {1, 1}, // sigFromSrams_bore_44_ram_hold
    {1, 1}, // sigFromSrams_bore_44_ram_bypass
    {1, 1}, // sigFromSrams_bore_44_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_44_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_44_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_44_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_44_cgen
    {1, 1}, // sigFromSrams_bore_45_ram_hold
    {1, 1}, // sigFromSrams_bore_45_ram_bypass
    {1, 1}, // sigFromSrams_bore_45_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_45_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_45_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_45_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_45_cgen
    {1, 1}, // sigFromSrams_bore_46_ram_hold
    {1, 1}, // sigFromSrams_bore_46_ram_bypass
    {1, 1}, // sigFromSrams_bore_46_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_46_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_46_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_46_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_46_cgen
    {1, 1}, // sigFromSrams_bore_47_ram_hold
    {1, 1}, // sigFromSrams_bore_47_ram_bypass
    {1, 1}, // sigFromSrams_bore_47_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_47_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_47_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_47_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_47_cgen
    {1, 1}, // sigFromSrams_bore_48_ram_hold
    {1, 1}, // sigFromSrams_bore_48_ram_bypass
    {1, 1}, // sigFromSrams_bore_48_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_48_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_48_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_48_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_48_cgen
    {1, 1}, // sigFromSrams_bore_49_ram_hold
    {1, 1}, // sigFromSrams_bore_49_ram_bypass
    {1, 1}, // sigFromSrams_bore_49_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_49_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_49_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_49_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_49_cgen
    {1, 1}, // sigFromSrams_bore_50_ram_hold
    {1, 1}, // sigFromSrams_bore_50_ram_bypass
    {1, 1}, // sigFromSrams_bore_50_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_50_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_50_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_50_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_50_cgen
    {1, 1}, // sigFromSrams_bore_51_ram_hold
    {1, 1}, // sigFromSrams_bore_51_ram_bypass
    {1, 1}, // sigFromSrams_bore_51_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_51_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_51_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_51_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_51_cgen
    {1, 1}, // sigFromSrams_bore_52_ram_hold
    {1, 1}, // sigFromSrams_bore_52_ram_bypass
    {1, 1}, // sigFromSrams_bore_52_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_52_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_52_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_52_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_52_cgen
    {1, 1}, // sigFromSrams_bore_53_ram_hold
    {1, 1}, // sigFromSrams_bore_53_ram_bypass
    {1, 1}, // sigFromSrams_bore_53_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_53_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_53_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_53_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_53_cgen
    {1, 1}, // sigFromSrams_bore_54_ram_hold
    {1, 1}, // sigFromSrams_bore_54_ram_bypass
    {1, 1}, // sigFromSrams_bore_54_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_54_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_54_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_54_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_54_cgen
    {1, 1}, // sigFromSrams_bore_55_ram_hold
    {1, 1}, // sigFromSrams_bore_55_ram_bypass
    {1, 1}, // sigFromSrams_bore_55_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_55_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_55_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_55_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_55_cgen
    {1, 1}, // sigFromSrams_bore_56_ram_hold
    {1, 1}, // sigFromSrams_bore_56_ram_bypass
    {1, 1}, // sigFromSrams_bore_56_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_56_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_56_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_56_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_56_cgen
    {1, 1}, // sigFromSrams_bore_57_ram_hold
    {1, 1}, // sigFromSrams_bore_57_ram_bypass
    {1, 1}, // sigFromSrams_bore_57_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_57_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_57_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_57_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_57_cgen
    {1, 1}, // sigFromSrams_bore_58_ram_hold
    {1, 1}, // sigFromSrams_bore_58_ram_bypass
    {1, 1}, // sigFromSrams_bore_58_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_58_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_58_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_58_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_58_cgen
    {1, 1}, // sigFromSrams_bore_59_ram_hold
    {1, 1}, // sigFromSrams_bore_59_ram_bypass
    {1, 1}, // sigFromSrams_bore_59_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_59_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_59_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_59_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_59_cgen
    {1, 1}, // sigFromSrams_bore_60_ram_hold
    {1, 1}, // sigFromSrams_bore_60_ram_bypass
    {1, 1}, // sigFromSrams_bore_60_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_60_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_60_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_60_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_60_cgen
    {1, 1}, // sigFromSrams_bore_61_ram_hold
    {1, 1}, // sigFromSrams_bore_61_ram_bypass
    {1, 1}, // sigFromSrams_bore_61_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_61_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_61_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_61_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_61_cgen
    {1, 1}, // sigFromSrams_bore_62_ram_hold
    {1, 1}, // sigFromSrams_bore_62_ram_bypass
    {1, 1}, // sigFromSrams_bore_62_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_62_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_62_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_62_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_62_cgen
    {1, 1}, // sigFromSrams_bore_63_ram_hold
    {1, 1}, // sigFromSrams_bore_63_ram_bypass
    {1, 1}, // sigFromSrams_bore_63_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_63_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_63_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_63_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_63_cgen
    {1, 1}, // sigFromSrams_bore_64_ram_hold
    {1, 1}, // sigFromSrams_bore_64_ram_bypass
    {1, 1}, // sigFromSrams_bore_64_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_64_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_64_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_64_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_64_cgen
    {1, 1}, // sigFromSrams_bore_65_ram_hold
    {1, 1}, // sigFromSrams_bore_65_ram_bypass
    {1, 1}, // sigFromSrams_bore_65_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_65_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_65_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_65_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_65_cgen
    {1, 1}, // sigFromSrams_bore_66_ram_hold
    {1, 1}, // sigFromSrams_bore_66_ram_bypass
    {1, 1}, // sigFromSrams_bore_66_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_66_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_66_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_66_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_66_cgen
    {1, 1}, // sigFromSrams_bore_67_ram_hold
    {1, 1}, // sigFromSrams_bore_67_ram_bypass
    {1, 1}, // sigFromSrams_bore_67_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_67_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_67_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_67_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_67_cgen
    {1, 1}, // sigFromSrams_bore_68_ram_hold
    {1, 1}, // sigFromSrams_bore_68_ram_bypass
    {1, 1}, // sigFromSrams_bore_68_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_68_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_68_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_68_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_68_cgen
    {1, 1}, // sigFromSrams_bore_69_ram_hold
    {1, 1}, // sigFromSrams_bore_69_ram_bypass
    {1, 1}, // sigFromSrams_bore_69_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_69_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_69_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_69_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_69_cgen
    {1, 1}, // sigFromSrams_bore_70_ram_hold
    {1, 1}, // sigFromSrams_bore_70_ram_bypass
    {1, 1}, // sigFromSrams_bore_70_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_70_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_70_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_70_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_70_cgen
    {1, 1}, // sigFromSrams_bore_71_ram_hold
    {1, 1}, // sigFromSrams_bore_71_ram_bypass
    {1, 1}, // sigFromSrams_bore_71_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_71_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_71_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_71_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_71_cgen
    {1, 1}, // sigFromSrams_bore_72_ram_hold
    {1, 1}, // sigFromSrams_bore_72_ram_bypass
    {1, 1}, // sigFromSrams_bore_72_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_72_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_72_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_72_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_72_cgen
    {1, 1}, // sigFromSrams_bore_73_ram_hold
    {1, 1}, // sigFromSrams_bore_73_ram_bypass
    {1, 1}, // sigFromSrams_bore_73_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_73_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_73_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_73_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_73_cgen
    {1, 1}, // sigFromSrams_bore_74_ram_hold
    {1, 1}, // sigFromSrams_bore_74_ram_bypass
    {1, 1}, // sigFromSrams_bore_74_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_74_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_74_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_74_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_74_cgen
    {1, 1}, // sigFromSrams_bore_75_ram_hold
    {1, 1}, // sigFromSrams_bore_75_ram_bypass
    {1, 1}, // sigFromSrams_bore_75_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_75_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_75_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_75_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_75_cgen
    {1, 1}, // sigFromSrams_bore_76_ram_hold
    {1, 1}, // sigFromSrams_bore_76_ram_bypass
    {1, 1}, // sigFromSrams_bore_76_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_76_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_76_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_76_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_76_cgen
    {1, 1}, // sigFromSrams_bore_77_ram_hold
    {1, 1}, // sigFromSrams_bore_77_ram_bypass
    {1, 1}, // sigFromSrams_bore_77_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_77_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_77_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_77_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_77_cgen
    {1, 1}, // sigFromSrams_bore_78_ram_hold
    {1, 1}, // sigFromSrams_bore_78_ram_bypass
    {1, 1}, // sigFromSrams_bore_78_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_78_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_78_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_78_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_78_cgen
    {1, 1}, // sigFromSrams_bore_79_ram_hold
    {1, 1}, // sigFromSrams_bore_79_ram_bypass
    {1, 1}, // sigFromSrams_bore_79_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_79_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_79_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_79_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_79_cgen
    {1, 1}, // sigFromSrams_bore_80_ram_hold
    {1, 1}, // sigFromSrams_bore_80_ram_bypass
    {1, 1}, // sigFromSrams_bore_80_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_80_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_80_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_80_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_80_cgen
    {1, 1}, // sigFromSrams_bore_81_ram_hold
    {1, 1}, // sigFromSrams_bore_81_ram_bypass
    {1, 1}, // sigFromSrams_bore_81_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_81_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_81_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_81_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_81_cgen
    {1, 1}, // sigFromSrams_bore_82_ram_hold
    {1, 1}, // sigFromSrams_bore_82_ram_bypass
    {1, 1}, // sigFromSrams_bore_82_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_82_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_82_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_82_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_82_cgen
    {1, 1}, // sigFromSrams_bore_83_ram_hold
    {1, 1}, // sigFromSrams_bore_83_ram_bypass
    {1, 1}, // sigFromSrams_bore_83_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_83_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_83_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_83_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_83_cgen
    {1, 1}, // sigFromSrams_bore_84_ram_hold
    {1, 1}, // sigFromSrams_bore_84_ram_bypass
    {1, 1}, // sigFromSrams_bore_84_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_84_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_84_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_84_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_84_cgen
    {1, 1}, // sigFromSrams_bore_85_ram_hold
    {1, 1}, // sigFromSrams_bore_85_ram_bypass
    {1, 1}, // sigFromSrams_bore_85_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_85_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_85_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_85_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_85_cgen
    {1, 1}, // sigFromSrams_bore_86_ram_hold
    {1, 1}, // sigFromSrams_bore_86_ram_bypass
    {1, 1}, // sigFromSrams_bore_86_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_86_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_86_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_86_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_86_cgen
    {1, 1}, // sigFromSrams_bore_87_ram_hold
    {1, 1}, // sigFromSrams_bore_87_ram_bypass
    {1, 1}, // sigFromSrams_bore_87_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_87_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_87_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_87_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_87_cgen
    {1, 1}, // sigFromSrams_bore_88_ram_hold
    {1, 1}, // sigFromSrams_bore_88_ram_bypass
    {1, 1}, // sigFromSrams_bore_88_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_88_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_88_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_88_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_88_cgen
    {1, 1}, // sigFromSrams_bore_89_ram_hold
    {1, 1}, // sigFromSrams_bore_89_ram_bypass
    {1, 1}, // sigFromSrams_bore_89_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_89_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_89_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_89_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_89_cgen
    {1, 1}, // sigFromSrams_bore_90_ram_hold
    {1, 1}, // sigFromSrams_bore_90_ram_bypass
    {1, 1}, // sigFromSrams_bore_90_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_90_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_90_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_90_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_90_cgen
    {1, 1}, // sigFromSrams_bore_91_ram_hold
    {1, 1}, // sigFromSrams_bore_91_ram_bypass
    {1, 1}, // sigFromSrams_bore_91_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_91_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_91_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_91_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_91_cgen
    {1, 1}, // sigFromSrams_bore_92_ram_hold
    {1, 1}, // sigFromSrams_bore_92_ram_bypass
    {1, 1}, // sigFromSrams_bore_92_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_92_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_92_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_92_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_92_cgen
    {1, 1}, // sigFromSrams_bore_93_ram_hold
    {1, 1}, // sigFromSrams_bore_93_ram_bypass
    {1, 1}, // sigFromSrams_bore_93_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_93_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_93_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_93_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_93_cgen
    {1, 1}, // sigFromSrams_bore_94_ram_hold
    {1, 1}, // sigFromSrams_bore_94_ram_bypass
    {1, 1}, // sigFromSrams_bore_94_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_94_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_94_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_94_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_94_cgen
    {1, 1}, // sigFromSrams_bore_95_ram_hold
    {1, 1}, // sigFromSrams_bore_95_ram_bypass
    {1, 1}, // sigFromSrams_bore_95_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_95_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_95_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_95_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_95_cgen
    {1, 1}, // sigFromSrams_bore_96_ram_hold
    {1, 1}, // sigFromSrams_bore_96_ram_bypass
    {1, 1}, // sigFromSrams_bore_96_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_96_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_96_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_96_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_96_cgen
    {1, 1}, // sigFromSrams_bore_97_ram_hold
    {1, 1}, // sigFromSrams_bore_97_ram_bypass
    {1, 1}, // sigFromSrams_bore_97_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_97_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_97_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_97_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_97_cgen
    {1, 1}, // sigFromSrams_bore_98_ram_hold
    {1, 1}, // sigFromSrams_bore_98_ram_bypass
    {1, 1}, // sigFromSrams_bore_98_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_98_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_98_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_98_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_98_cgen
    {1, 1}, // sigFromSrams_bore_99_ram_hold
    {1, 1}, // sigFromSrams_bore_99_ram_bypass
    {1, 1}, // sigFromSrams_bore_99_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_99_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_99_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_99_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_99_cgen
    {1, 1}, // sigFromSrams_bore_100_ram_hold
    {1, 1}, // sigFromSrams_bore_100_ram_bypass
    {1, 1}, // sigFromSrams_bore_100_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_100_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_100_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_100_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_100_cgen
    {1, 1}, // sigFromSrams_bore_101_ram_hold
    {1, 1}, // sigFromSrams_bore_101_ram_bypass
    {1, 1}, // sigFromSrams_bore_101_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_101_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_101_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_101_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_101_cgen
    {1, 1}, // sigFromSrams_bore_102_ram_hold
    {1, 1}, // sigFromSrams_bore_102_ram_bypass
    {1, 1}, // sigFromSrams_bore_102_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_102_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_102_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_102_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_102_cgen
    {1, 1}, // sigFromSrams_bore_103_ram_hold
    {1, 1}, // sigFromSrams_bore_103_ram_bypass
    {1, 1}, // sigFromSrams_bore_103_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_103_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_103_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_103_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_103_cgen
    {1, 1}, // sigFromSrams_bore_104_ram_hold
    {1, 1}, // sigFromSrams_bore_104_ram_bypass
    {1, 1}, // sigFromSrams_bore_104_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_104_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_104_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_104_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_104_cgen
    {1, 1}, // sigFromSrams_bore_105_ram_hold
    {1, 1}, // sigFromSrams_bore_105_ram_bypass
    {1, 1}, // sigFromSrams_bore_105_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_105_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_105_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_105_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_105_cgen
    {1, 1}, // sigFromSrams_bore_106_ram_hold
    {1, 1}, // sigFromSrams_bore_106_ram_bypass
    {1, 1}, // sigFromSrams_bore_106_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_106_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_106_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_106_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_106_cgen
    {1, 1}, // sigFromSrams_bore_107_ram_hold
    {1, 1}, // sigFromSrams_bore_107_ram_bypass
    {1, 1}, // sigFromSrams_bore_107_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_107_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_107_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_107_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_107_cgen
    {1, 1}, // sigFromSrams_bore_108_ram_hold
    {1, 1}, // sigFromSrams_bore_108_ram_bypass
    {1, 1}, // sigFromSrams_bore_108_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_108_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_108_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_108_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_108_cgen
    {1, 1}, // sigFromSrams_bore_109_ram_hold
    {1, 1}, // sigFromSrams_bore_109_ram_bypass
    {1, 1}, // sigFromSrams_bore_109_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_109_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_109_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_109_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_109_cgen
    {1, 1}, // sigFromSrams_bore_110_ram_hold
    {1, 1}, // sigFromSrams_bore_110_ram_bypass
    {1, 1}, // sigFromSrams_bore_110_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_110_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_110_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_110_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_110_cgen
    {1, 1}, // sigFromSrams_bore_111_ram_hold
    {1, 1}, // sigFromSrams_bore_111_ram_bypass
    {1, 1}, // sigFromSrams_bore_111_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_111_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_111_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_111_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_111_cgen
    {1, 1}, // sigFromSrams_bore_112_ram_hold
    {1, 1}, // sigFromSrams_bore_112_ram_bypass
    {1, 1}, // sigFromSrams_bore_112_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_112_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_112_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_112_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_112_cgen
    {1, 1}, // sigFromSrams_bore_113_ram_hold
    {1, 1}, // sigFromSrams_bore_113_ram_bypass
    {1, 1}, // sigFromSrams_bore_113_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_113_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_113_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_113_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_113_cgen
    {1, 1}, // sigFromSrams_bore_114_ram_hold
    {1, 1}, // sigFromSrams_bore_114_ram_bypass
    {1, 1}, // sigFromSrams_bore_114_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_114_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_114_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_114_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_114_cgen
    {1, 1}, // sigFromSrams_bore_115_ram_hold
    {1, 1}, // sigFromSrams_bore_115_ram_bypass
    {1, 1}, // sigFromSrams_bore_115_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_115_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_115_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_115_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_115_cgen
    {1, 1}, // sigFromSrams_bore_116_ram_hold
    {1, 1}, // sigFromSrams_bore_116_ram_bypass
    {1, 1}, // sigFromSrams_bore_116_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_116_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_116_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_116_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_116_cgen
    {1, 1}, // sigFromSrams_bore_117_ram_hold
    {1, 1}, // sigFromSrams_bore_117_ram_bypass
    {1, 1}, // sigFromSrams_bore_117_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_117_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_117_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_117_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_117_cgen
    {1, 1}, // sigFromSrams_bore_118_ram_hold
    {1, 1}, // sigFromSrams_bore_118_ram_bypass
    {1, 1}, // sigFromSrams_bore_118_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_118_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_118_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_118_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_118_cgen
    {1, 1}, // sigFromSrams_bore_119_ram_hold
    {1, 1}, // sigFromSrams_bore_119_ram_bypass
    {1, 1}, // sigFromSrams_bore_119_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_119_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_119_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_119_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_119_cgen
    {1, 1}, // sigFromSrams_bore_120_ram_hold
    {1, 1}, // sigFromSrams_bore_120_ram_bypass
    {1, 1}, // sigFromSrams_bore_120_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_120_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_120_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_120_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_120_cgen
    {1, 1}, // sigFromSrams_bore_121_ram_hold
    {1, 1}, // sigFromSrams_bore_121_ram_bypass
    {1, 1}, // sigFromSrams_bore_121_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_121_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_121_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_121_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_121_cgen
    {1, 1}, // sigFromSrams_bore_122_ram_hold
    {1, 1}, // sigFromSrams_bore_122_ram_bypass
    {1, 1}, // sigFromSrams_bore_122_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_122_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_122_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_122_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_122_cgen
    {1, 1}, // sigFromSrams_bore_123_ram_hold
    {1, 1}, // sigFromSrams_bore_123_ram_bypass
    {1, 1}, // sigFromSrams_bore_123_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_123_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_123_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_123_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_123_cgen
    {1, 1}, // sigFromSrams_bore_124_ram_hold
    {1, 1}, // sigFromSrams_bore_124_ram_bypass
    {1, 1}, // sigFromSrams_bore_124_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_124_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_124_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_124_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_124_cgen
    {1, 1}, // sigFromSrams_bore_125_ram_hold
    {1, 1}, // sigFromSrams_bore_125_ram_bypass
    {1, 1}, // sigFromSrams_bore_125_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_125_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_125_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_125_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_125_cgen
    {1, 1}, // sigFromSrams_bore_126_ram_hold
    {1, 1}, // sigFromSrams_bore_126_ram_bypass
    {1, 1}, // sigFromSrams_bore_126_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_126_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_126_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_126_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_126_cgen
    {1, 1}, // sigFromSrams_bore_127_ram_hold
    {1, 1}, // sigFromSrams_bore_127_ram_bypass
    {1, 1}, // sigFromSrams_bore_127_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_127_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_127_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_127_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_127_cgen
    {1, 1}, // sigFromSrams_bore_128_ram_hold
    {1, 1}, // sigFromSrams_bore_128_ram_bypass
    {1, 1}, // sigFromSrams_bore_128_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_128_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_128_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_128_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_128_cgen
    {1, 1}, // sigFromSrams_bore_129_ram_hold
    {1, 1}, // sigFromSrams_bore_129_ram_bypass
    {1, 1}, // sigFromSrams_bore_129_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_129_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_129_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_129_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_129_cgen
    {1, 1}, // sigFromSrams_bore_130_ram_hold
    {1, 1}, // sigFromSrams_bore_130_ram_bypass
    {1, 1}, // sigFromSrams_bore_130_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_130_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_130_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_130_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_130_cgen
    {1, 1}, // sigFromSrams_bore_131_ram_hold
    {1, 1}, // sigFromSrams_bore_131_ram_bypass
    {1, 1}, // sigFromSrams_bore_131_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_131_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_131_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_131_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_131_cgen
    {1, 1}, // sigFromSrams_bore_132_ram_hold
    {1, 1}, // sigFromSrams_bore_132_ram_bypass
    {1, 1}, // sigFromSrams_bore_132_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_132_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_132_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_132_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_132_cgen
    {1, 1}, // sigFromSrams_bore_133_ram_hold
    {1, 1}, // sigFromSrams_bore_133_ram_bypass
    {1, 1}, // sigFromSrams_bore_133_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_133_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_133_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_133_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_133_cgen
    {1, 1}, // sigFromSrams_bore_134_ram_hold
    {1, 1}, // sigFromSrams_bore_134_ram_bypass
    {1, 1}, // sigFromSrams_bore_134_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_134_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_134_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_134_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_134_cgen
    {1, 1}, // sigFromSrams_bore_135_ram_hold
    {1, 1}, // sigFromSrams_bore_135_ram_bypass
    {1, 1}, // sigFromSrams_bore_135_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_135_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_135_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_135_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_135_cgen
    {1, 1}, // sigFromSrams_bore_136_ram_hold
    {1, 1}, // sigFromSrams_bore_136_ram_bypass
    {1, 1}, // sigFromSrams_bore_136_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_136_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_136_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_136_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_136_cgen
    {1, 1}, // sigFromSrams_bore_137_ram_hold
    {1, 1}, // sigFromSrams_bore_137_ram_bypass
    {1, 1}, // sigFromSrams_bore_137_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_137_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_137_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_137_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_137_cgen
    {1, 1}, // sigFromSrams_bore_138_ram_hold
    {1, 1}, // sigFromSrams_bore_138_ram_bypass
    {1, 1}, // sigFromSrams_bore_138_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_138_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_138_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_138_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_138_cgen
    {1, 1}, // sigFromSrams_bore_139_ram_hold
    {1, 1}, // sigFromSrams_bore_139_ram_bypass
    {1, 1}, // sigFromSrams_bore_139_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_139_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_139_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_139_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_139_cgen
    {1, 1}, // sigFromSrams_bore_140_ram_hold
    {1, 1}, // sigFromSrams_bore_140_ram_bypass
    {1, 1}, // sigFromSrams_bore_140_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_140_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_140_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_140_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_140_cgen
    {1, 1}, // sigFromSrams_bore_141_ram_hold
    {1, 1}, // sigFromSrams_bore_141_ram_bypass
    {1, 1}, // sigFromSrams_bore_141_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_141_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_141_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_141_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_141_cgen
    {1, 1}, // sigFromSrams_bore_142_ram_hold
    {1, 1}, // sigFromSrams_bore_142_ram_bypass
    {1, 1}, // sigFromSrams_bore_142_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_142_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_142_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_142_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_142_cgen
    {1, 1}, // sigFromSrams_bore_143_ram_hold
    {1, 1}, // sigFromSrams_bore_143_ram_bypass
    {1, 1}, // sigFromSrams_bore_143_ram_bp_clken
    {1, 1}, // sigFromSrams_bore_143_ram_aux_clk
    {1, 1}, // sigFromSrams_bore_143_ram_aux_ckbp
    {1, 1}, // sigFromSrams_bore_143_ram_mcp_hold
    {1, 1}, // sigFromSrams_bore_143_cgen
};

static bool parse_event_line(const std::string &line, Event &event) {
    if (line.empty()) { return false; }
    std::stringstream ss(line);
    std::string field;
    if (!std::getline(ss, field, ","[0])) return false;
    event.time = std::stoi(field);
    if (!std::getline(ss, field, ","[0])) return false;
    event.signal_id = std::stoi(field);
    if (!std::getline(ss, field)) return false;
    event.value = field;
    return true;
}

static void parse_value(const std::string &value, int width, std::vector<uint32_t> &words, uint64_t &scalar) {
    const int word_count = (width + 31) / 32;
    words.assign(word_count, 0);
    int bit = 0;
    for (int i = static_cast<int>(value.size()) - 1; i >= 0 && bit < width; --i, ++bit) {
        const char c = value[i];
        if (c == "1"[0]) {
            const int word = bit / 32;
            const int offset = bit % 32;
            words[word] |= (1u << offset);
        }
    }
    scalar = 0;
    if (width <= 64) {
        scalar = words[0];
        if (word_count > 1) {
            scalar |= (static_cast<uint64_t>(words[1]) << 32);
        }
    }
}

static void apply_signal(VRef *ref, VWolf *wolf, int signal_id, const std::vector<uint32_t> &words, uint64_t scalar) {
    switch (signal_id) {
    case 0:
        ref->clock = as_scalar<decltype(ref->clock)>(scalar);
        wolf->clock = as_scalar<decltype(wolf->clock)>(scalar);
        break;
    case 1:
        ref->reset = as_scalar<decltype(ref->reset)>(scalar);
        wolf->reset = as_scalar<decltype(wolf->reset)>(scalar);
        break;
    case 2:
        ref->io_ctrl_ubtbEnable = as_scalar<decltype(ref->io_ctrl_ubtbEnable)>(scalar);
        wolf->io_ctrl_ubtbEnable = as_scalar<decltype(wolf->io_ctrl_ubtbEnable)>(scalar);
        break;
    case 3:
        ref->io_ctrl_abtbEnable = as_scalar<decltype(ref->io_ctrl_abtbEnable)>(scalar);
        wolf->io_ctrl_abtbEnable = as_scalar<decltype(wolf->io_ctrl_abtbEnable)>(scalar);
        break;
    case 4:
        ref->io_ctrl_mbtbEnable = as_scalar<decltype(ref->io_ctrl_mbtbEnable)>(scalar);
        wolf->io_ctrl_mbtbEnable = as_scalar<decltype(wolf->io_ctrl_mbtbEnable)>(scalar);
        break;
    case 5:
        ref->io_ctrl_tageEnable = as_scalar<decltype(ref->io_ctrl_tageEnable)>(scalar);
        wolf->io_ctrl_tageEnable = as_scalar<decltype(wolf->io_ctrl_tageEnable)>(scalar);
        break;
    case 6:
        ref->io_ctrl_scEnable = as_scalar<decltype(ref->io_ctrl_scEnable)>(scalar);
        wolf->io_ctrl_scEnable = as_scalar<decltype(wolf->io_ctrl_scEnable)>(scalar);
        break;
    case 7:
        ref->io_ctrl_ittageEnable = as_scalar<decltype(ref->io_ctrl_ittageEnable)>(scalar);
        wolf->io_ctrl_ittageEnable = as_scalar<decltype(wolf->io_ctrl_ittageEnable)>(scalar);
        break;
    case 8:
        ref->io_resetVector_addr = as_scalar<decltype(ref->io_resetVector_addr)>(scalar);
        wolf->io_resetVector_addr = as_scalar<decltype(wolf->io_resetVector_addr)>(scalar);
        break;
    case 9:
        ref->io_fromFtq_redirect_valid = as_scalar<decltype(ref->io_fromFtq_redirect_valid)>(scalar);
        wolf->io_fromFtq_redirect_valid = as_scalar<decltype(wolf->io_fromFtq_redirect_valid)>(scalar);
        break;
    case 10:
        ref->io_fromFtq_redirect_bits_cfiPc_addr = as_scalar<decltype(ref->io_fromFtq_redirect_bits_cfiPc_addr)>(scalar);
        wolf->io_fromFtq_redirect_bits_cfiPc_addr = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_cfiPc_addr)>(scalar);
        break;
    case 11:
        ref->io_fromFtq_redirect_bits_target_addr = as_scalar<decltype(ref->io_fromFtq_redirect_bits_target_addr)>(scalar);
        wolf->io_fromFtq_redirect_bits_target_addr = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_target_addr)>(scalar);
        break;
    case 12:
        ref->io_fromFtq_redirect_bits_taken = as_scalar<decltype(ref->io_fromFtq_redirect_bits_taken)>(scalar);
        wolf->io_fromFtq_redirect_bits_taken = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_taken)>(scalar);
        break;
    case 13:
        ref->io_fromFtq_redirect_bits_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_redirect_bits_attribute_branchType)>(scalar);
        wolf->io_fromFtq_redirect_bits_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_attribute_branchType)>(scalar);
        break;
    case 14:
        ref->io_fromFtq_redirect_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_redirect_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_redirect_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_attribute_rasAction)>(scalar);
        break;
    case 15:
        ref->io_fromFtq_redirect_bits_meta_phr_phrPtr_flag = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_phr_phrPtr_flag)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_phr_phrPtr_flag = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_phr_phrPtr_flag)>(scalar);
        break;
    case 16:
        ref->io_fromFtq_redirect_bits_meta_phr_phrPtr_value = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_phr_phrPtr_value)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_phr_phrPtr_value = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_phr_phrPtr_value)>(scalar);
        break;
    case 17:
        ref->io_fromFtq_redirect_bits_meta_phr_phrLowBits = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_phr_phrLowBits)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_phr_phrLowBits = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_phr_phrLowBits)>(scalar);
        break;
    case 18:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_ghr = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_ghr)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_ghr = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_ghr)>(scalar);
        break;
    case 19:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_bw = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_bw)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_bw = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_bw)>(scalar);
        break;
    case 20:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_0 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_0)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_0 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_0)>(scalar);
        break;
    case 21:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_1 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_1)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_1 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_1)>(scalar);
        break;
    case 22:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_2 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_2)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_2 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_2)>(scalar);
        break;
    case 23:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_3 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_3)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_3 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_3)>(scalar);
        break;
    case 24:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_4 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_4)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_4 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_4)>(scalar);
        break;
    case 25:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_5 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_5)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_5 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_5)>(scalar);
        break;
    case 26:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_6 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_6)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_6 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_6)>(scalar);
        break;
    case 27:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_7 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_7)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_7 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_7)>(scalar);
        break;
    case 28:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_0_branchType = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_0_branchType)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_0_branchType = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_0_branchType)>(scalar);
        break;
    case 29:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_1_branchType = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_1_branchType)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_1_branchType = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_1_branchType)>(scalar);
        break;
    case 30:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_2_branchType = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_2_branchType)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_2_branchType = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_2_branchType)>(scalar);
        break;
    case 31:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_3_branchType = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_3_branchType)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_3_branchType = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_3_branchType)>(scalar);
        break;
    case 32:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_4_branchType = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_4_branchType)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_4_branchType = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_4_branchType)>(scalar);
        break;
    case 33:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_5_branchType = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_5_branchType)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_5_branchType = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_5_branchType)>(scalar);
        break;
    case 34:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_6_branchType = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_6_branchType)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_6_branchType = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_6_branchType)>(scalar);
        break;
    case 35:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_7_branchType = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_7_branchType)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_7_branchType = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_7_branchType)>(scalar);
        break;
    case 36:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_0 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_0)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_0 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_0)>(scalar);
        break;
    case 37:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_1 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_1)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_1 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_1)>(scalar);
        break;
    case 38:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_2 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_2)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_2 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_2)>(scalar);
        break;
    case 39:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_3 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_3)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_3 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_3)>(scalar);
        break;
    case 40:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_4 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_4)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_4 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_4)>(scalar);
        break;
    case 41:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_5 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_5)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_5 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_5)>(scalar);
        break;
    case 42:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_6 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_6)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_6 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_6)>(scalar);
        break;
    case 43:
        ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_7 = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_commonHRMeta_position_7)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_7 = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_commonHRMeta_position_7)>(scalar);
        break;
    case 44:
        ref->io_fromFtq_redirect_bits_meta_ras_ssp = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_ras_ssp)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_ras_ssp = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_ras_ssp)>(scalar);
        break;
    case 45:
        ref->io_fromFtq_redirect_bits_meta_ras_sctr = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_ras_sctr)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_ras_sctr = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_ras_sctr)>(scalar);
        break;
    case 46:
        ref->io_fromFtq_redirect_bits_meta_ras_tosw_flag = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_ras_tosw_flag)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_ras_tosw_flag = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_ras_tosw_flag)>(scalar);
        break;
    case 47:
        ref->io_fromFtq_redirect_bits_meta_ras_tosw_value = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_ras_tosw_value)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_ras_tosw_value = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_ras_tosw_value)>(scalar);
        break;
    case 48:
        ref->io_fromFtq_redirect_bits_meta_ras_tosr_flag = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_ras_tosr_flag)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_ras_tosr_flag = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_ras_tosr_flag)>(scalar);
        break;
    case 49:
        ref->io_fromFtq_redirect_bits_meta_ras_tosr_value = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_ras_tosr_value)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_ras_tosr_value = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_ras_tosr_value)>(scalar);
        break;
    case 50:
        ref->io_fromFtq_redirect_bits_meta_ras_nos_flag = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_ras_nos_flag)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_ras_nos_flag = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_ras_nos_flag)>(scalar);
        break;
    case 51:
        ref->io_fromFtq_redirect_bits_meta_ras_nos_value = as_scalar<decltype(ref->io_fromFtq_redirect_bits_meta_ras_nos_value)>(scalar);
        wolf->io_fromFtq_redirect_bits_meta_ras_nos_value = as_scalar<decltype(wolf->io_fromFtq_redirect_bits_meta_ras_nos_value)>(scalar);
        break;
    case 52:
        ref->io_fromFtq_train_valid = as_scalar<decltype(ref->io_fromFtq_train_valid)>(scalar);
        wolf->io_fromFtq_train_valid = as_scalar<decltype(wolf->io_fromFtq_train_valid)>(scalar);
        break;
    case 53:
        ref->io_fromFtq_train_bits_startPc_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_startPc_addr)>(scalar);
        wolf->io_fromFtq_train_bits_startPc_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_startPc_addr)>(scalar);
        break;
    case 54:
        ref->io_fromFtq_train_bits_branches_0_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_0_valid)>(scalar);
        wolf->io_fromFtq_train_bits_branches_0_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_0_valid)>(scalar);
        break;
    case 55:
        ref->io_fromFtq_train_bits_branches_0_bits_target_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_0_bits_target_addr)>(scalar);
        wolf->io_fromFtq_train_bits_branches_0_bits_target_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_0_bits_target_addr)>(scalar);
        break;
    case 56:
        ref->io_fromFtq_train_bits_branches_0_bits_taken = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_0_bits_taken)>(scalar);
        wolf->io_fromFtq_train_bits_branches_0_bits_taken = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_0_bits_taken)>(scalar);
        break;
    case 57:
        ref->io_fromFtq_train_bits_branches_0_bits_cfiPosition = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_0_bits_cfiPosition)>(scalar);
        wolf->io_fromFtq_train_bits_branches_0_bits_cfiPosition = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_0_bits_cfiPosition)>(scalar);
        break;
    case 58:
        ref->io_fromFtq_train_bits_branches_0_bits_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_0_bits_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_branches_0_bits_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_0_bits_attribute_branchType)>(scalar);
        break;
    case 59:
        ref->io_fromFtq_train_bits_branches_0_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_0_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_branches_0_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_0_bits_attribute_rasAction)>(scalar);
        break;
    case 60:
        ref->io_fromFtq_train_bits_branches_0_bits_mispredict = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_0_bits_mispredict)>(scalar);
        wolf->io_fromFtq_train_bits_branches_0_bits_mispredict = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_0_bits_mispredict)>(scalar);
        break;
    case 61:
        ref->io_fromFtq_train_bits_branches_1_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_1_valid)>(scalar);
        wolf->io_fromFtq_train_bits_branches_1_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_1_valid)>(scalar);
        break;
    case 62:
        ref->io_fromFtq_train_bits_branches_1_bits_target_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_1_bits_target_addr)>(scalar);
        wolf->io_fromFtq_train_bits_branches_1_bits_target_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_1_bits_target_addr)>(scalar);
        break;
    case 63:
        ref->io_fromFtq_train_bits_branches_1_bits_taken = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_1_bits_taken)>(scalar);
        wolf->io_fromFtq_train_bits_branches_1_bits_taken = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_1_bits_taken)>(scalar);
        break;
    case 64:
        ref->io_fromFtq_train_bits_branches_1_bits_cfiPosition = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_1_bits_cfiPosition)>(scalar);
        wolf->io_fromFtq_train_bits_branches_1_bits_cfiPosition = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_1_bits_cfiPosition)>(scalar);
        break;
    case 65:
        ref->io_fromFtq_train_bits_branches_1_bits_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_1_bits_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_branches_1_bits_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_1_bits_attribute_branchType)>(scalar);
        break;
    case 66:
        ref->io_fromFtq_train_bits_branches_1_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_1_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_branches_1_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_1_bits_attribute_rasAction)>(scalar);
        break;
    case 67:
        ref->io_fromFtq_train_bits_branches_1_bits_mispredict = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_1_bits_mispredict)>(scalar);
        wolf->io_fromFtq_train_bits_branches_1_bits_mispredict = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_1_bits_mispredict)>(scalar);
        break;
    case 68:
        ref->io_fromFtq_train_bits_branches_2_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_2_valid)>(scalar);
        wolf->io_fromFtq_train_bits_branches_2_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_2_valid)>(scalar);
        break;
    case 69:
        ref->io_fromFtq_train_bits_branches_2_bits_target_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_2_bits_target_addr)>(scalar);
        wolf->io_fromFtq_train_bits_branches_2_bits_target_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_2_bits_target_addr)>(scalar);
        break;
    case 70:
        ref->io_fromFtq_train_bits_branches_2_bits_taken = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_2_bits_taken)>(scalar);
        wolf->io_fromFtq_train_bits_branches_2_bits_taken = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_2_bits_taken)>(scalar);
        break;
    case 71:
        ref->io_fromFtq_train_bits_branches_2_bits_cfiPosition = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_2_bits_cfiPosition)>(scalar);
        wolf->io_fromFtq_train_bits_branches_2_bits_cfiPosition = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_2_bits_cfiPosition)>(scalar);
        break;
    case 72:
        ref->io_fromFtq_train_bits_branches_2_bits_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_2_bits_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_branches_2_bits_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_2_bits_attribute_branchType)>(scalar);
        break;
    case 73:
        ref->io_fromFtq_train_bits_branches_2_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_2_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_branches_2_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_2_bits_attribute_rasAction)>(scalar);
        break;
    case 74:
        ref->io_fromFtq_train_bits_branches_2_bits_mispredict = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_2_bits_mispredict)>(scalar);
        wolf->io_fromFtq_train_bits_branches_2_bits_mispredict = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_2_bits_mispredict)>(scalar);
        break;
    case 75:
        ref->io_fromFtq_train_bits_branches_3_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_3_valid)>(scalar);
        wolf->io_fromFtq_train_bits_branches_3_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_3_valid)>(scalar);
        break;
    case 76:
        ref->io_fromFtq_train_bits_branches_3_bits_target_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_3_bits_target_addr)>(scalar);
        wolf->io_fromFtq_train_bits_branches_3_bits_target_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_3_bits_target_addr)>(scalar);
        break;
    case 77:
        ref->io_fromFtq_train_bits_branches_3_bits_taken = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_3_bits_taken)>(scalar);
        wolf->io_fromFtq_train_bits_branches_3_bits_taken = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_3_bits_taken)>(scalar);
        break;
    case 78:
        ref->io_fromFtq_train_bits_branches_3_bits_cfiPosition = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_3_bits_cfiPosition)>(scalar);
        wolf->io_fromFtq_train_bits_branches_3_bits_cfiPosition = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_3_bits_cfiPosition)>(scalar);
        break;
    case 79:
        ref->io_fromFtq_train_bits_branches_3_bits_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_3_bits_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_branches_3_bits_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_3_bits_attribute_branchType)>(scalar);
        break;
    case 80:
        ref->io_fromFtq_train_bits_branches_3_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_3_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_branches_3_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_3_bits_attribute_rasAction)>(scalar);
        break;
    case 81:
        ref->io_fromFtq_train_bits_branches_3_bits_mispredict = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_3_bits_mispredict)>(scalar);
        wolf->io_fromFtq_train_bits_branches_3_bits_mispredict = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_3_bits_mispredict)>(scalar);
        break;
    case 82:
        ref->io_fromFtq_train_bits_branches_4_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_4_valid)>(scalar);
        wolf->io_fromFtq_train_bits_branches_4_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_4_valid)>(scalar);
        break;
    case 83:
        ref->io_fromFtq_train_bits_branches_4_bits_target_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_4_bits_target_addr)>(scalar);
        wolf->io_fromFtq_train_bits_branches_4_bits_target_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_4_bits_target_addr)>(scalar);
        break;
    case 84:
        ref->io_fromFtq_train_bits_branches_4_bits_taken = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_4_bits_taken)>(scalar);
        wolf->io_fromFtq_train_bits_branches_4_bits_taken = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_4_bits_taken)>(scalar);
        break;
    case 85:
        ref->io_fromFtq_train_bits_branches_4_bits_cfiPosition = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_4_bits_cfiPosition)>(scalar);
        wolf->io_fromFtq_train_bits_branches_4_bits_cfiPosition = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_4_bits_cfiPosition)>(scalar);
        break;
    case 86:
        ref->io_fromFtq_train_bits_branches_4_bits_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_4_bits_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_branches_4_bits_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_4_bits_attribute_branchType)>(scalar);
        break;
    case 87:
        ref->io_fromFtq_train_bits_branches_4_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_4_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_branches_4_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_4_bits_attribute_rasAction)>(scalar);
        break;
    case 88:
        ref->io_fromFtq_train_bits_branches_4_bits_mispredict = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_4_bits_mispredict)>(scalar);
        wolf->io_fromFtq_train_bits_branches_4_bits_mispredict = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_4_bits_mispredict)>(scalar);
        break;
    case 89:
        ref->io_fromFtq_train_bits_branches_5_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_5_valid)>(scalar);
        wolf->io_fromFtq_train_bits_branches_5_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_5_valid)>(scalar);
        break;
    case 90:
        ref->io_fromFtq_train_bits_branches_5_bits_target_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_5_bits_target_addr)>(scalar);
        wolf->io_fromFtq_train_bits_branches_5_bits_target_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_5_bits_target_addr)>(scalar);
        break;
    case 91:
        ref->io_fromFtq_train_bits_branches_5_bits_taken = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_5_bits_taken)>(scalar);
        wolf->io_fromFtq_train_bits_branches_5_bits_taken = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_5_bits_taken)>(scalar);
        break;
    case 92:
        ref->io_fromFtq_train_bits_branches_5_bits_cfiPosition = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_5_bits_cfiPosition)>(scalar);
        wolf->io_fromFtq_train_bits_branches_5_bits_cfiPosition = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_5_bits_cfiPosition)>(scalar);
        break;
    case 93:
        ref->io_fromFtq_train_bits_branches_5_bits_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_5_bits_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_branches_5_bits_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_5_bits_attribute_branchType)>(scalar);
        break;
    case 94:
        ref->io_fromFtq_train_bits_branches_5_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_5_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_branches_5_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_5_bits_attribute_rasAction)>(scalar);
        break;
    case 95:
        ref->io_fromFtq_train_bits_branches_5_bits_mispredict = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_5_bits_mispredict)>(scalar);
        wolf->io_fromFtq_train_bits_branches_5_bits_mispredict = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_5_bits_mispredict)>(scalar);
        break;
    case 96:
        ref->io_fromFtq_train_bits_branches_6_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_6_valid)>(scalar);
        wolf->io_fromFtq_train_bits_branches_6_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_6_valid)>(scalar);
        break;
    case 97:
        ref->io_fromFtq_train_bits_branches_6_bits_target_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_6_bits_target_addr)>(scalar);
        wolf->io_fromFtq_train_bits_branches_6_bits_target_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_6_bits_target_addr)>(scalar);
        break;
    case 98:
        ref->io_fromFtq_train_bits_branches_6_bits_taken = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_6_bits_taken)>(scalar);
        wolf->io_fromFtq_train_bits_branches_6_bits_taken = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_6_bits_taken)>(scalar);
        break;
    case 99:
        ref->io_fromFtq_train_bits_branches_6_bits_cfiPosition = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_6_bits_cfiPosition)>(scalar);
        wolf->io_fromFtq_train_bits_branches_6_bits_cfiPosition = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_6_bits_cfiPosition)>(scalar);
        break;
    case 100:
        ref->io_fromFtq_train_bits_branches_6_bits_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_6_bits_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_branches_6_bits_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_6_bits_attribute_branchType)>(scalar);
        break;
    case 101:
        ref->io_fromFtq_train_bits_branches_6_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_6_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_branches_6_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_6_bits_attribute_rasAction)>(scalar);
        break;
    case 102:
        ref->io_fromFtq_train_bits_branches_6_bits_mispredict = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_6_bits_mispredict)>(scalar);
        wolf->io_fromFtq_train_bits_branches_6_bits_mispredict = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_6_bits_mispredict)>(scalar);
        break;
    case 103:
        ref->io_fromFtq_train_bits_branches_7_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_7_valid)>(scalar);
        wolf->io_fromFtq_train_bits_branches_7_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_7_valid)>(scalar);
        break;
    case 104:
        ref->io_fromFtq_train_bits_branches_7_bits_target_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_7_bits_target_addr)>(scalar);
        wolf->io_fromFtq_train_bits_branches_7_bits_target_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_7_bits_target_addr)>(scalar);
        break;
    case 105:
        ref->io_fromFtq_train_bits_branches_7_bits_taken = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_7_bits_taken)>(scalar);
        wolf->io_fromFtq_train_bits_branches_7_bits_taken = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_7_bits_taken)>(scalar);
        break;
    case 106:
        ref->io_fromFtq_train_bits_branches_7_bits_cfiPosition = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_7_bits_cfiPosition)>(scalar);
        wolf->io_fromFtq_train_bits_branches_7_bits_cfiPosition = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_7_bits_cfiPosition)>(scalar);
        break;
    case 107:
        ref->io_fromFtq_train_bits_branches_7_bits_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_7_bits_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_branches_7_bits_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_7_bits_attribute_branchType)>(scalar);
        break;
    case 108:
        ref->io_fromFtq_train_bits_branches_7_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_7_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_branches_7_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_7_bits_attribute_rasAction)>(scalar);
        break;
    case 109:
        ref->io_fromFtq_train_bits_branches_7_bits_mispredict = as_scalar<decltype(ref->io_fromFtq_train_bits_branches_7_bits_mispredict)>(scalar);
        wolf->io_fromFtq_train_bits_branches_7_bits_mispredict = as_scalar<decltype(wolf->io_fromFtq_train_bits_branches_7_bits_mispredict)>(scalar);
        break;
    case 110:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_rawHit = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_rawHit)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_rawHit = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_rawHit)>(scalar);
        break;
    case 111:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_position = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_position)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_position = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_position)>(scalar);
        break;
    case 112:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_branchType)>(scalar);
        break;
    case 113:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_rasAction)>(scalar);
        break;
    case 114:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_counter_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_0_counter_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_counter_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_0_counter_value)>(scalar);
        break;
    case 115:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_rawHit = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_rawHit)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_rawHit = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_rawHit)>(scalar);
        break;
    case 116:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_position = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_position)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_position = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_position)>(scalar);
        break;
    case 117:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_branchType)>(scalar);
        break;
    case 118:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_rasAction)>(scalar);
        break;
    case 119:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_counter_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_1_counter_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_counter_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_1_counter_value)>(scalar);
        break;
    case 120:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_rawHit = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_rawHit)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_rawHit = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_rawHit)>(scalar);
        break;
    case 121:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_position = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_position)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_position = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_position)>(scalar);
        break;
    case 122:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_branchType)>(scalar);
        break;
    case 123:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_rasAction)>(scalar);
        break;
    case 124:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_counter_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_2_counter_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_counter_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_2_counter_value)>(scalar);
        break;
    case 125:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_rawHit = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_rawHit)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_rawHit = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_rawHit)>(scalar);
        break;
    case 126:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_position = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_position)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_position = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_position)>(scalar);
        break;
    case 127:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_branchType)>(scalar);
        break;
    case 128:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_rasAction)>(scalar);
        break;
    case 129:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_counter_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_0_3_counter_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_counter_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_0_3_counter_value)>(scalar);
        break;
    case 130:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_rawHit = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_rawHit)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_rawHit = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_rawHit)>(scalar);
        break;
    case 131:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_position = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_position)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_position = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_position)>(scalar);
        break;
    case 132:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_branchType)>(scalar);
        break;
    case 133:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_rasAction)>(scalar);
        break;
    case 134:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_counter_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_0_counter_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_counter_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_0_counter_value)>(scalar);
        break;
    case 135:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_rawHit = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_rawHit)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_rawHit = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_rawHit)>(scalar);
        break;
    case 136:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_position = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_position)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_position = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_position)>(scalar);
        break;
    case 137:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_branchType)>(scalar);
        break;
    case 138:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_rasAction)>(scalar);
        break;
    case 139:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_counter_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_1_counter_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_counter_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_1_counter_value)>(scalar);
        break;
    case 140:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_rawHit = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_rawHit)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_rawHit = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_rawHit)>(scalar);
        break;
    case 141:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_position = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_position)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_position = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_position)>(scalar);
        break;
    case 142:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_branchType)>(scalar);
        break;
    case 143:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_rasAction)>(scalar);
        break;
    case 144:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_counter_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_2_counter_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_counter_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_2_counter_value)>(scalar);
        break;
    case 145:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_rawHit = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_rawHit)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_rawHit = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_rawHit)>(scalar);
        break;
    case 146:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_position = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_position)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_position = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_position)>(scalar);
        break;
    case 147:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_branchType = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_branchType)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_branchType = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_branchType)>(scalar);
        break;
    case 148:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_rasAction)>(scalar);
        break;
    case 149:
        ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_counter_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_mbtb_entries_1_3_counter_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_counter_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_mbtb_entries_1_3_counter_value)>(scalar);
        break;
    case 150:
        ref->io_fromFtq_train_bits_meta_tage_entries_0_useProvider = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_0_useProvider)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_0_useProvider = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_0_useProvider)>(scalar);
        break;
    case 151:
        ref->io_fromFtq_train_bits_meta_tage_entries_0_providerTableIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_0_providerTableIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_0_providerTableIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_0_providerTableIdx)>(scalar);
        break;
    case 152:
        ref->io_fromFtq_train_bits_meta_tage_entries_0_providerWayIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_0_providerWayIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_0_providerWayIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_0_providerWayIdx)>(scalar);
        break;
    case 153:
        ref->io_fromFtq_train_bits_meta_tage_entries_0_providerTakenCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_0_providerTakenCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_0_providerTakenCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_0_providerTakenCtr_value)>(scalar);
        break;
    case 154:
        ref->io_fromFtq_train_bits_meta_tage_entries_0_providerUsefulCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_0_providerUsefulCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_0_providerUsefulCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_0_providerUsefulCtr_value)>(scalar);
        break;
    case 155:
        ref->io_fromFtq_train_bits_meta_tage_entries_0_altOrBasePred = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_0_altOrBasePred)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_0_altOrBasePred = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_0_altOrBasePred)>(scalar);
        break;
    case 156:
        ref->io_fromFtq_train_bits_meta_tage_entries_1_useProvider = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_1_useProvider)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_1_useProvider = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_1_useProvider)>(scalar);
        break;
    case 157:
        ref->io_fromFtq_train_bits_meta_tage_entries_1_providerTableIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_1_providerTableIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_1_providerTableIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_1_providerTableIdx)>(scalar);
        break;
    case 158:
        ref->io_fromFtq_train_bits_meta_tage_entries_1_providerWayIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_1_providerWayIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_1_providerWayIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_1_providerWayIdx)>(scalar);
        break;
    case 159:
        ref->io_fromFtq_train_bits_meta_tage_entries_1_providerTakenCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_1_providerTakenCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_1_providerTakenCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_1_providerTakenCtr_value)>(scalar);
        break;
    case 160:
        ref->io_fromFtq_train_bits_meta_tage_entries_1_providerUsefulCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_1_providerUsefulCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_1_providerUsefulCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_1_providerUsefulCtr_value)>(scalar);
        break;
    case 161:
        ref->io_fromFtq_train_bits_meta_tage_entries_1_altOrBasePred = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_1_altOrBasePred)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_1_altOrBasePred = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_1_altOrBasePred)>(scalar);
        break;
    case 162:
        ref->io_fromFtq_train_bits_meta_tage_entries_2_useProvider = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_2_useProvider)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_2_useProvider = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_2_useProvider)>(scalar);
        break;
    case 163:
        ref->io_fromFtq_train_bits_meta_tage_entries_2_providerTableIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_2_providerTableIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_2_providerTableIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_2_providerTableIdx)>(scalar);
        break;
    case 164:
        ref->io_fromFtq_train_bits_meta_tage_entries_2_providerWayIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_2_providerWayIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_2_providerWayIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_2_providerWayIdx)>(scalar);
        break;
    case 165:
        ref->io_fromFtq_train_bits_meta_tage_entries_2_providerTakenCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_2_providerTakenCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_2_providerTakenCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_2_providerTakenCtr_value)>(scalar);
        break;
    case 166:
        ref->io_fromFtq_train_bits_meta_tage_entries_2_providerUsefulCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_2_providerUsefulCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_2_providerUsefulCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_2_providerUsefulCtr_value)>(scalar);
        break;
    case 167:
        ref->io_fromFtq_train_bits_meta_tage_entries_2_altOrBasePred = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_2_altOrBasePred)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_2_altOrBasePred = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_2_altOrBasePred)>(scalar);
        break;
    case 168:
        ref->io_fromFtq_train_bits_meta_tage_entries_3_useProvider = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_3_useProvider)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_3_useProvider = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_3_useProvider)>(scalar);
        break;
    case 169:
        ref->io_fromFtq_train_bits_meta_tage_entries_3_providerTableIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_3_providerTableIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_3_providerTableIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_3_providerTableIdx)>(scalar);
        break;
    case 170:
        ref->io_fromFtq_train_bits_meta_tage_entries_3_providerWayIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_3_providerWayIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_3_providerWayIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_3_providerWayIdx)>(scalar);
        break;
    case 171:
        ref->io_fromFtq_train_bits_meta_tage_entries_3_providerTakenCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_3_providerTakenCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_3_providerTakenCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_3_providerTakenCtr_value)>(scalar);
        break;
    case 172:
        ref->io_fromFtq_train_bits_meta_tage_entries_3_providerUsefulCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_3_providerUsefulCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_3_providerUsefulCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_3_providerUsefulCtr_value)>(scalar);
        break;
    case 173:
        ref->io_fromFtq_train_bits_meta_tage_entries_3_altOrBasePred = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_3_altOrBasePred)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_3_altOrBasePred = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_3_altOrBasePred)>(scalar);
        break;
    case 174:
        ref->io_fromFtq_train_bits_meta_tage_entries_4_useProvider = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_4_useProvider)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_4_useProvider = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_4_useProvider)>(scalar);
        break;
    case 175:
        ref->io_fromFtq_train_bits_meta_tage_entries_4_providerTableIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_4_providerTableIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_4_providerTableIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_4_providerTableIdx)>(scalar);
        break;
    case 176:
        ref->io_fromFtq_train_bits_meta_tage_entries_4_providerWayIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_4_providerWayIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_4_providerWayIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_4_providerWayIdx)>(scalar);
        break;
    case 177:
        ref->io_fromFtq_train_bits_meta_tage_entries_4_providerTakenCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_4_providerTakenCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_4_providerTakenCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_4_providerTakenCtr_value)>(scalar);
        break;
    case 178:
        ref->io_fromFtq_train_bits_meta_tage_entries_4_providerUsefulCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_4_providerUsefulCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_4_providerUsefulCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_4_providerUsefulCtr_value)>(scalar);
        break;
    case 179:
        ref->io_fromFtq_train_bits_meta_tage_entries_4_altOrBasePred = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_4_altOrBasePred)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_4_altOrBasePred = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_4_altOrBasePred)>(scalar);
        break;
    case 180:
        ref->io_fromFtq_train_bits_meta_tage_entries_5_useProvider = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_5_useProvider)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_5_useProvider = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_5_useProvider)>(scalar);
        break;
    case 181:
        ref->io_fromFtq_train_bits_meta_tage_entries_5_providerTableIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_5_providerTableIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_5_providerTableIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_5_providerTableIdx)>(scalar);
        break;
    case 182:
        ref->io_fromFtq_train_bits_meta_tage_entries_5_providerWayIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_5_providerWayIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_5_providerWayIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_5_providerWayIdx)>(scalar);
        break;
    case 183:
        ref->io_fromFtq_train_bits_meta_tage_entries_5_providerTakenCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_5_providerTakenCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_5_providerTakenCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_5_providerTakenCtr_value)>(scalar);
        break;
    case 184:
        ref->io_fromFtq_train_bits_meta_tage_entries_5_providerUsefulCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_5_providerUsefulCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_5_providerUsefulCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_5_providerUsefulCtr_value)>(scalar);
        break;
    case 185:
        ref->io_fromFtq_train_bits_meta_tage_entries_5_altOrBasePred = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_5_altOrBasePred)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_5_altOrBasePred = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_5_altOrBasePred)>(scalar);
        break;
    case 186:
        ref->io_fromFtq_train_bits_meta_tage_entries_6_useProvider = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_6_useProvider)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_6_useProvider = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_6_useProvider)>(scalar);
        break;
    case 187:
        ref->io_fromFtq_train_bits_meta_tage_entries_6_providerTableIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_6_providerTableIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_6_providerTableIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_6_providerTableIdx)>(scalar);
        break;
    case 188:
        ref->io_fromFtq_train_bits_meta_tage_entries_6_providerWayIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_6_providerWayIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_6_providerWayIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_6_providerWayIdx)>(scalar);
        break;
    case 189:
        ref->io_fromFtq_train_bits_meta_tage_entries_6_providerTakenCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_6_providerTakenCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_6_providerTakenCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_6_providerTakenCtr_value)>(scalar);
        break;
    case 190:
        ref->io_fromFtq_train_bits_meta_tage_entries_6_providerUsefulCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_6_providerUsefulCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_6_providerUsefulCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_6_providerUsefulCtr_value)>(scalar);
        break;
    case 191:
        ref->io_fromFtq_train_bits_meta_tage_entries_6_altOrBasePred = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_6_altOrBasePred)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_6_altOrBasePred = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_6_altOrBasePred)>(scalar);
        break;
    case 192:
        ref->io_fromFtq_train_bits_meta_tage_entries_7_useProvider = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_7_useProvider)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_7_useProvider = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_7_useProvider)>(scalar);
        break;
    case 193:
        ref->io_fromFtq_train_bits_meta_tage_entries_7_providerTableIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_7_providerTableIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_7_providerTableIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_7_providerTableIdx)>(scalar);
        break;
    case 194:
        ref->io_fromFtq_train_bits_meta_tage_entries_7_providerWayIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_7_providerWayIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_7_providerWayIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_7_providerWayIdx)>(scalar);
        break;
    case 195:
        ref->io_fromFtq_train_bits_meta_tage_entries_7_providerTakenCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_7_providerTakenCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_7_providerTakenCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_7_providerTakenCtr_value)>(scalar);
        break;
    case 196:
        ref->io_fromFtq_train_bits_meta_tage_entries_7_providerUsefulCtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_7_providerUsefulCtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_7_providerUsefulCtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_7_providerUsefulCtr_value)>(scalar);
        break;
    case 197:
        ref->io_fromFtq_train_bits_meta_tage_entries_7_altOrBasePred = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_tage_entries_7_altOrBasePred)>(scalar);
        wolf->io_fromFtq_train_bits_meta_tage_entries_7_altOrBasePred = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_tage_entries_7_altOrBasePred)>(scalar);
        break;
    case 198:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_0)>(scalar);
        break;
    case 199:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_1)>(scalar);
        break;
    case 200:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_2)>(scalar);
        break;
    case 201:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_3)>(scalar);
        break;
    case 202:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_4)>(scalar);
        break;
    case 203:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_5)>(scalar);
        break;
    case 204:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_6)>(scalar);
        break;
    case 205:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_0_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_0_7)>(scalar);
        break;
    case 206:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_0)>(scalar);
        break;
    case 207:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_1)>(scalar);
        break;
    case 208:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_2)>(scalar);
        break;
    case 209:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_3)>(scalar);
        break;
    case 210:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_4)>(scalar);
        break;
    case 211:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_5)>(scalar);
        break;
    case 212:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_6)>(scalar);
        break;
    case 213:
        ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPathResp_1_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPathResp_1_7)>(scalar);
        break;
    case 214:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_0)>(scalar);
        break;
    case 215:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_1)>(scalar);
        break;
    case 216:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_2)>(scalar);
        break;
    case 217:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_3)>(scalar);
        break;
    case 218:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_4)>(scalar);
        break;
    case 219:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_5)>(scalar);
        break;
    case 220:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_6)>(scalar);
        break;
    case 221:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_7)>(scalar);
        break;
    case 222:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_8 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_8)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_8 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_8)>(scalar);
        break;
    case 223:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_9 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_9)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_9 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_9)>(scalar);
        break;
    case 224:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_10 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_10)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_10 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_10)>(scalar);
        break;
    case 225:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_11 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_11)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_11 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_11)>(scalar);
        break;
    case 226:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_12 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_12)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_12 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_12)>(scalar);
        break;
    case 227:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_13 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_13)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_13 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_13)>(scalar);
        break;
    case 228:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_14 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_14)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_14 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_14)>(scalar);
        break;
    case 229:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_15 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_15)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_15 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_15)>(scalar);
        break;
    case 230:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_16 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_16)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_16 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_16)>(scalar);
        break;
    case 231:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_17 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_17)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_17 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_17)>(scalar);
        break;
    case 232:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_18 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_18)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_18 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_18)>(scalar);
        break;
    case 233:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_19 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_19)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_19 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_19)>(scalar);
        break;
    case 234:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_20 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_20)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_20 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_20)>(scalar);
        break;
    case 235:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_21 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_21)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_21 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_21)>(scalar);
        break;
    case 236:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_22 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_22)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_22 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_22)>(scalar);
        break;
    case 237:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_23 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_23)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_23 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_23)>(scalar);
        break;
    case 238:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_24 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_24)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_24 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_24)>(scalar);
        break;
    case 239:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_25 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_25)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_25 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_25)>(scalar);
        break;
    case 240:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_26 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_26)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_26 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_26)>(scalar);
        break;
    case 241:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_27 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_27)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_27 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_27)>(scalar);
        break;
    case 242:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_28 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_28)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_28 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_28)>(scalar);
        break;
    case 243:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_29 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_29)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_29 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_29)>(scalar);
        break;
    case 244:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_30 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_30)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_30 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_30)>(scalar);
        break;
    case 245:
        ref->io_fromFtq_train_bits_meta_sc_scBiasResp_31 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasResp_31)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_31 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasResp_31)>(scalar);
        break;
    case 246:
        ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_0)>(scalar);
        break;
    case 247:
        ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_1)>(scalar);
        break;
    case 248:
        ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_2)>(scalar);
        break;
    case 249:
        ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_3)>(scalar);
        break;
    case 250:
        ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_4)>(scalar);
        break;
    case 251:
        ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_5)>(scalar);
        break;
    case 252:
        ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_6)>(scalar);
        break;
    case 253:
        ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scBiasLowerBits_7)>(scalar);
        break;
    case 254:
        ref->io_fromFtq_train_bits_meta_sc_scCommonHR_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scCommonHR_valid)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scCommonHR_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scCommonHR_valid)>(scalar);
        break;
    case 255:
        ref->io_fromFtq_train_bits_meta_sc_scCommonHR_ghr = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scCommonHR_ghr)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scCommonHR_ghr = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scCommonHR_ghr)>(scalar);
        break;
    case 256:
        ref->io_fromFtq_train_bits_meta_sc_scCommonHR_bw = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scCommonHR_bw)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scCommonHR_bw = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scCommonHR_bw)>(scalar);
        break;
    case 257:
        ref->io_fromFtq_train_bits_meta_sc_scPred_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPred_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPred_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPred_0)>(scalar);
        break;
    case 258:
        ref->io_fromFtq_train_bits_meta_sc_scPred_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPred_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPred_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPred_1)>(scalar);
        break;
    case 259:
        ref->io_fromFtq_train_bits_meta_sc_scPred_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPred_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPred_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPred_2)>(scalar);
        break;
    case 260:
        ref->io_fromFtq_train_bits_meta_sc_scPred_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPred_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPred_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPred_3)>(scalar);
        break;
    case 261:
        ref->io_fromFtq_train_bits_meta_sc_scPred_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPred_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPred_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPred_4)>(scalar);
        break;
    case 262:
        ref->io_fromFtq_train_bits_meta_sc_scPred_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPred_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPred_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPred_5)>(scalar);
        break;
    case 263:
        ref->io_fromFtq_train_bits_meta_sc_scPred_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPred_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPred_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPred_6)>(scalar);
        break;
    case 264:
        ref->io_fromFtq_train_bits_meta_sc_scPred_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_scPred_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_scPred_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_scPred_7)>(scalar);
        break;
    case 265:
        ref->io_fromFtq_train_bits_meta_sc_tagePred_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePred_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePred_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePred_0)>(scalar);
        break;
    case 266:
        ref->io_fromFtq_train_bits_meta_sc_tagePred_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePred_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePred_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePred_1)>(scalar);
        break;
    case 267:
        ref->io_fromFtq_train_bits_meta_sc_tagePred_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePred_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePred_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePred_2)>(scalar);
        break;
    case 268:
        ref->io_fromFtq_train_bits_meta_sc_tagePred_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePred_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePred_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePred_3)>(scalar);
        break;
    case 269:
        ref->io_fromFtq_train_bits_meta_sc_tagePred_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePred_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePred_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePred_4)>(scalar);
        break;
    case 270:
        ref->io_fromFtq_train_bits_meta_sc_tagePred_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePred_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePred_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePred_5)>(scalar);
        break;
    case 271:
        ref->io_fromFtq_train_bits_meta_sc_tagePred_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePred_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePred_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePred_6)>(scalar);
        break;
    case 272:
        ref->io_fromFtq_train_bits_meta_sc_tagePred_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePred_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePred_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePred_7)>(scalar);
        break;
    case 273:
        ref->io_fromFtq_train_bits_meta_sc_tagePredValid_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePredValid_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_0)>(scalar);
        break;
    case 274:
        ref->io_fromFtq_train_bits_meta_sc_tagePredValid_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePredValid_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_1)>(scalar);
        break;
    case 275:
        ref->io_fromFtq_train_bits_meta_sc_tagePredValid_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePredValid_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_2)>(scalar);
        break;
    case 276:
        ref->io_fromFtq_train_bits_meta_sc_tagePredValid_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePredValid_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_3)>(scalar);
        break;
    case 277:
        ref->io_fromFtq_train_bits_meta_sc_tagePredValid_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePredValid_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_4)>(scalar);
        break;
    case 278:
        ref->io_fromFtq_train_bits_meta_sc_tagePredValid_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePredValid_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_5)>(scalar);
        break;
    case 279:
        ref->io_fromFtq_train_bits_meta_sc_tagePredValid_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePredValid_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_6)>(scalar);
        break;
    case 280:
        ref->io_fromFtq_train_bits_meta_sc_tagePredValid_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_tagePredValid_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_tagePredValid_7)>(scalar);
        break;
    case 281:
        ref->io_fromFtq_train_bits_meta_sc_useScPred_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_useScPred_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_useScPred_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_useScPred_0)>(scalar);
        break;
    case 282:
        ref->io_fromFtq_train_bits_meta_sc_useScPred_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_useScPred_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_useScPred_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_useScPred_1)>(scalar);
        break;
    case 283:
        ref->io_fromFtq_train_bits_meta_sc_useScPred_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_useScPred_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_useScPred_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_useScPred_2)>(scalar);
        break;
    case 284:
        ref->io_fromFtq_train_bits_meta_sc_useScPred_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_useScPred_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_useScPred_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_useScPred_3)>(scalar);
        break;
    case 285:
        ref->io_fromFtq_train_bits_meta_sc_useScPred_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_useScPred_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_useScPred_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_useScPred_4)>(scalar);
        break;
    case 286:
        ref->io_fromFtq_train_bits_meta_sc_useScPred_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_useScPred_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_useScPred_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_useScPred_5)>(scalar);
        break;
    case 287:
        ref->io_fromFtq_train_bits_meta_sc_useScPred_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_useScPred_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_useScPred_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_useScPred_6)>(scalar);
        break;
    case 288:
        ref->io_fromFtq_train_bits_meta_sc_useScPred_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_useScPred_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_useScPred_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_useScPred_7)>(scalar);
        break;
    case 289:
        ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_0)>(scalar);
        break;
    case 290:
        ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_1)>(scalar);
        break;
    case 291:
        ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_2)>(scalar);
        break;
    case 292:
        ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_3)>(scalar);
        break;
    case 293:
        ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_4)>(scalar);
        break;
    case 294:
        ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_5)>(scalar);
        break;
    case 295:
        ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_6)>(scalar);
        break;
    case 296:
        ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_sumAboveThres_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_sumAboveThres_7)>(scalar);
        break;
    case 297:
        ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_0)>(scalar);
        break;
    case 298:
        ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_1)>(scalar);
        break;
    case 299:
        ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_2)>(scalar);
        break;
    case 300:
        ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_3)>(scalar);
        break;
    case 301:
        ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_4)>(scalar);
        break;
    case 302:
        ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_5)>(scalar);
        break;
    case 303:
        ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_6)>(scalar);
        break;
    case 304:
        ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_7)>(scalar);
        break;
    case 305:
        ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_0)>(scalar);
        break;
    case 306:
        ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_1)>(scalar);
        break;
    case 307:
        ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_2 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_2)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_2 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_2)>(scalar);
        break;
    case 308:
        ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_3 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_3)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_3 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_3)>(scalar);
        break;
    case 309:
        ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_4 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_4)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_4 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_4)>(scalar);
        break;
    case 310:
        ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_5 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_5)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_5 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_5)>(scalar);
        break;
    case 311:
        ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_6 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_6)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_6 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_6)>(scalar);
        break;
    case 312:
        ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_7 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_7)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_7 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_7)>(scalar);
        break;
    case 313:
        ref->io_fromFtq_train_bits_meta_sc_debug_predPathIdx_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_predPathIdx_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_predPathIdx_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_predPathIdx_0)>(scalar);
        break;
    case 314:
        ref->io_fromFtq_train_bits_meta_sc_debug_predPathIdx_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_predPathIdx_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_predPathIdx_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_predPathIdx_1)>(scalar);
        break;
    case 315:
        ref->io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_0)>(scalar);
        break;
    case 316:
        ref->io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_1)>(scalar);
        break;
    case 317:
        ref->io_fromFtq_train_bits_meta_sc_debug_predBWIdx_0 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_predBWIdx_0)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_predBWIdx_0 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_predBWIdx_0)>(scalar);
        break;
    case 318:
        ref->io_fromFtq_train_bits_meta_sc_debug_predBWIdx_1 = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_predBWIdx_1)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_predBWIdx_1 = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_predBWIdx_1)>(scalar);
        break;
    case 319:
        ref->io_fromFtq_train_bits_meta_sc_debug_predBiasIdx = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_sc_debug_predBiasIdx)>(scalar);
        wolf->io_fromFtq_train_bits_meta_sc_debug_predBiasIdx = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_sc_debug_predBiasIdx)>(scalar);
        break;
    case 320:
        ref->io_fromFtq_train_bits_meta_ittage_provider_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_provider_valid)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_provider_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_provider_valid)>(scalar);
        break;
    case 321:
        ref->io_fromFtq_train_bits_meta_ittage_provider_bits = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_provider_bits)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_provider_bits = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_provider_bits)>(scalar);
        break;
    case 322:
        ref->io_fromFtq_train_bits_meta_ittage_altProvider_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_altProvider_valid)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_altProvider_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_altProvider_valid)>(scalar);
        break;
    case 323:
        ref->io_fromFtq_train_bits_meta_ittage_altProvider_bits = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_altProvider_bits)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_altProvider_bits = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_altProvider_bits)>(scalar);
        break;
    case 324:
        ref->io_fromFtq_train_bits_meta_ittage_altDiffers = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_altDiffers)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_altDiffers = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_altDiffers)>(scalar);
        break;
    case 325:
        ref->io_fromFtq_train_bits_meta_ittage_providerUsefulCnt_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_providerUsefulCnt_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_providerUsefulCnt_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_providerUsefulCnt_value)>(scalar);
        break;
    case 326:
        ref->io_fromFtq_train_bits_meta_ittage_providerCnt_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_providerCnt_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_providerCnt_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_providerCnt_value)>(scalar);
        break;
    case 327:
        ref->io_fromFtq_train_bits_meta_ittage_altProviderCnt_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_altProviderCnt_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_altProviderCnt_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_altProviderCnt_value)>(scalar);
        break;
    case 328:
        ref->io_fromFtq_train_bits_meta_ittage_allocate_valid = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_allocate_valid)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_allocate_valid = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_allocate_valid)>(scalar);
        break;
    case 329:
        ref->io_fromFtq_train_bits_meta_ittage_allocate_bits = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_allocate_bits)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_allocate_bits = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_allocate_bits)>(scalar);
        break;
    case 330:
        ref->io_fromFtq_train_bits_meta_ittage_providerTarget_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_providerTarget_addr)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_providerTarget_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_providerTarget_addr)>(scalar);
        break;
    case 331:
        ref->io_fromFtq_train_bits_meta_ittage_altProviderTarget_addr = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_ittage_altProviderTarget_addr)>(scalar);
        wolf->io_fromFtq_train_bits_meta_ittage_altProviderTarget_addr = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_ittage_altProviderTarget_addr)>(scalar);
        break;
    case 332:
        ref->io_fromFtq_train_bits_meta_phr_phrPtr_value = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_phrPtr_value)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_phrPtr_value = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_phrPtr_value)>(scalar);
        break;
    case 333:
        ref->io_fromFtq_train_bits_meta_phr_phrLowBits = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_phrLowBits)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_phrLowBits = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_phrLowBits)>(scalar);
        break;
    case 334:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist)>(scalar);
        break;
    case 335:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist)>(scalar);
        break;
    case 336:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist)>(scalar);
        break;
    case 337:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist)>(scalar);
        break;
    case 338:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist)>(scalar);
        break;
    case 339:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist)>(scalar);
        break;
    case 340:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist)>(scalar);
        break;
    case 341:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist)>(scalar);
        break;
    case 342:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist)>(scalar);
        break;
    case 343:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist)>(scalar);
        break;
    case 344:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist)>(scalar);
        break;
    case 345:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist)>(scalar);
        break;
    case 346:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist)>(scalar);
        break;
    case 347:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist)>(scalar);
        break;
    case 348:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist)>(scalar);
        break;
    case 349:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist)>(scalar);
        break;
    case 350:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist)>(scalar);
        break;
    case 351:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist)>(scalar);
        break;
    case 352:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist)>(scalar);
        break;
    case 353:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist)>(scalar);
        break;
    case 354:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist)>(scalar);
        break;
    case 355:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist)>(scalar);
        break;
    case 356:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist)>(scalar);
        break;
    case 357:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist)>(scalar);
        break;
    case 358:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist)>(scalar);
        break;
    case 359:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist)>(scalar);
        break;
    case 360:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist)>(scalar);
        break;
    case 361:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist)>(scalar);
        break;
    case 362:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist)>(scalar);
        break;
    case 363:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist)>(scalar);
        break;
    case 364:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist)>(scalar);
        break;
    case 365:
        ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist = as_scalar<decltype(ref->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist)>(scalar);
        wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist = as_scalar<decltype(wolf->io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist)>(scalar);
        break;
    case 366:
        ref->io_fromFtq_commit_valid = as_scalar<decltype(ref->io_fromFtq_commit_valid)>(scalar);
        wolf->io_fromFtq_commit_valid = as_scalar<decltype(wolf->io_fromFtq_commit_valid)>(scalar);
        break;
    case 367:
        ref->io_fromFtq_commit_bits_meta_ras_ssp = as_scalar<decltype(ref->io_fromFtq_commit_bits_meta_ras_ssp)>(scalar);
        wolf->io_fromFtq_commit_bits_meta_ras_ssp = as_scalar<decltype(wolf->io_fromFtq_commit_bits_meta_ras_ssp)>(scalar);
        break;
    case 368:
        ref->io_fromFtq_commit_bits_meta_ras_tosw_flag = as_scalar<decltype(ref->io_fromFtq_commit_bits_meta_ras_tosw_flag)>(scalar);
        wolf->io_fromFtq_commit_bits_meta_ras_tosw_flag = as_scalar<decltype(wolf->io_fromFtq_commit_bits_meta_ras_tosw_flag)>(scalar);
        break;
    case 369:
        ref->io_fromFtq_commit_bits_meta_ras_tosw_value = as_scalar<decltype(ref->io_fromFtq_commit_bits_meta_ras_tosw_value)>(scalar);
        wolf->io_fromFtq_commit_bits_meta_ras_tosw_value = as_scalar<decltype(wolf->io_fromFtq_commit_bits_meta_ras_tosw_value)>(scalar);
        break;
    case 370:
        ref->io_fromFtq_commit_bits_attribute_rasAction = as_scalar<decltype(ref->io_fromFtq_commit_bits_attribute_rasAction)>(scalar);
        wolf->io_fromFtq_commit_bits_attribute_rasAction = as_scalar<decltype(wolf->io_fromFtq_commit_bits_attribute_rasAction)>(scalar);
        break;
    case 371:
        ref->io_fromFtq_bpuPtr_flag = as_scalar<decltype(ref->io_fromFtq_bpuPtr_flag)>(scalar);
        wolf->io_fromFtq_bpuPtr_flag = as_scalar<decltype(wolf->io_fromFtq_bpuPtr_flag)>(scalar);
        break;
    case 372:
        ref->io_fromFtq_bpuPtr_value = as_scalar<decltype(ref->io_fromFtq_bpuPtr_value)>(scalar);
        wolf->io_fromFtq_bpuPtr_value = as_scalar<decltype(wolf->io_fromFtq_bpuPtr_value)>(scalar);
        break;
    case 373:
        ref->io_toFtq_prediction_ready = as_scalar<decltype(ref->io_toFtq_prediction_ready)>(scalar);
        wolf->io_toFtq_prediction_ready = as_scalar<decltype(wolf->io_toFtq_prediction_ready)>(scalar);
        break;
    case 374:
        ref->boreChildrenBd_bore_array = as_scalar<decltype(ref->boreChildrenBd_bore_array)>(scalar);
        wolf->boreChildrenBd_bore_array = as_scalar<decltype(wolf->boreChildrenBd_bore_array)>(scalar);
        break;
    case 375:
        ref->boreChildrenBd_bore_all = as_scalar<decltype(ref->boreChildrenBd_bore_all)>(scalar);
        wolf->boreChildrenBd_bore_all = as_scalar<decltype(wolf->boreChildrenBd_bore_all)>(scalar);
        break;
    case 376:
        ref->boreChildrenBd_bore_req = as_scalar<decltype(ref->boreChildrenBd_bore_req)>(scalar);
        wolf->boreChildrenBd_bore_req = as_scalar<decltype(wolf->boreChildrenBd_bore_req)>(scalar);
        break;
    case 377:
        ref->boreChildrenBd_bore_writeen = as_scalar<decltype(ref->boreChildrenBd_bore_writeen)>(scalar);
        wolf->boreChildrenBd_bore_writeen = as_scalar<decltype(wolf->boreChildrenBd_bore_writeen)>(scalar);
        break;
    case 378:
        ref->boreChildrenBd_bore_be = as_scalar<decltype(ref->boreChildrenBd_bore_be)>(scalar);
        wolf->boreChildrenBd_bore_be = as_scalar<decltype(wolf->boreChildrenBd_bore_be)>(scalar);
        break;
    case 379:
        ref->boreChildrenBd_bore_addr = as_scalar<decltype(ref->boreChildrenBd_bore_addr)>(scalar);
        wolf->boreChildrenBd_bore_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_addr)>(scalar);
        break;
    case 380:
        for (int i = 0; i < 4; ++i) {
            ref->boreChildrenBd_bore_indata.data()[i] = words[i];
            wolf->boreChildrenBd_bore_indata.data()[i] = words[i];
        }
        break;
    case 381:
        ref->boreChildrenBd_bore_readen = as_scalar<decltype(ref->boreChildrenBd_bore_readen)>(scalar);
        wolf->boreChildrenBd_bore_readen = as_scalar<decltype(wolf->boreChildrenBd_bore_readen)>(scalar);
        break;
    case 382:
        ref->boreChildrenBd_bore_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_addr_rd)>(scalar);
        break;
    case 383:
        ref->boreChildrenBd_bore_1_array = as_scalar<decltype(ref->boreChildrenBd_bore_1_array)>(scalar);
        wolf->boreChildrenBd_bore_1_array = as_scalar<decltype(wolf->boreChildrenBd_bore_1_array)>(scalar);
        break;
    case 384:
        ref->boreChildrenBd_bore_1_all = as_scalar<decltype(ref->boreChildrenBd_bore_1_all)>(scalar);
        wolf->boreChildrenBd_bore_1_all = as_scalar<decltype(wolf->boreChildrenBd_bore_1_all)>(scalar);
        break;
    case 385:
        ref->boreChildrenBd_bore_1_req = as_scalar<decltype(ref->boreChildrenBd_bore_1_req)>(scalar);
        wolf->boreChildrenBd_bore_1_req = as_scalar<decltype(wolf->boreChildrenBd_bore_1_req)>(scalar);
        break;
    case 386:
        ref->boreChildrenBd_bore_1_writeen = as_scalar<decltype(ref->boreChildrenBd_bore_1_writeen)>(scalar);
        wolf->boreChildrenBd_bore_1_writeen = as_scalar<decltype(wolf->boreChildrenBd_bore_1_writeen)>(scalar);
        break;
    case 387:
        ref->boreChildrenBd_bore_1_be = as_scalar<decltype(ref->boreChildrenBd_bore_1_be)>(scalar);
        wolf->boreChildrenBd_bore_1_be = as_scalar<decltype(wolf->boreChildrenBd_bore_1_be)>(scalar);
        break;
    case 388:
        ref->boreChildrenBd_bore_1_addr = as_scalar<decltype(ref->boreChildrenBd_bore_1_addr)>(scalar);
        wolf->boreChildrenBd_bore_1_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_1_addr)>(scalar);
        break;
    case 389:
        ref->boreChildrenBd_bore_1_indata = as_scalar<decltype(ref->boreChildrenBd_bore_1_indata)>(scalar);
        wolf->boreChildrenBd_bore_1_indata = as_scalar<decltype(wolf->boreChildrenBd_bore_1_indata)>(scalar);
        break;
    case 390:
        ref->boreChildrenBd_bore_1_readen = as_scalar<decltype(ref->boreChildrenBd_bore_1_readen)>(scalar);
        wolf->boreChildrenBd_bore_1_readen = as_scalar<decltype(wolf->boreChildrenBd_bore_1_readen)>(scalar);
        break;
    case 391:
        ref->boreChildrenBd_bore_1_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_1_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_1_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_1_addr_rd)>(scalar);
        break;
    case 392:
        ref->boreChildrenBd_bore_2_array = as_scalar<decltype(ref->boreChildrenBd_bore_2_array)>(scalar);
        wolf->boreChildrenBd_bore_2_array = as_scalar<decltype(wolf->boreChildrenBd_bore_2_array)>(scalar);
        break;
    case 393:
        ref->boreChildrenBd_bore_2_all = as_scalar<decltype(ref->boreChildrenBd_bore_2_all)>(scalar);
        wolf->boreChildrenBd_bore_2_all = as_scalar<decltype(wolf->boreChildrenBd_bore_2_all)>(scalar);
        break;
    case 394:
        ref->boreChildrenBd_bore_2_req = as_scalar<decltype(ref->boreChildrenBd_bore_2_req)>(scalar);
        wolf->boreChildrenBd_bore_2_req = as_scalar<decltype(wolf->boreChildrenBd_bore_2_req)>(scalar);
        break;
    case 395:
        ref->boreChildrenBd_bore_2_writeen = as_scalar<decltype(ref->boreChildrenBd_bore_2_writeen)>(scalar);
        wolf->boreChildrenBd_bore_2_writeen = as_scalar<decltype(wolf->boreChildrenBd_bore_2_writeen)>(scalar);
        break;
    case 396:
        for (int i = 0; i < 3; ++i) {
            ref->boreChildrenBd_bore_2_be.data()[i] = words[i];
            wolf->boreChildrenBd_bore_2_be.data()[i] = words[i];
        }
        break;
    case 397:
        ref->boreChildrenBd_bore_2_addr = as_scalar<decltype(ref->boreChildrenBd_bore_2_addr)>(scalar);
        wolf->boreChildrenBd_bore_2_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_2_addr)>(scalar);
        break;
    case 398:
        for (int i = 0; i < 3; ++i) {
            ref->boreChildrenBd_bore_2_indata.data()[i] = words[i];
            wolf->boreChildrenBd_bore_2_indata.data()[i] = words[i];
        }
        break;
    case 399:
        ref->boreChildrenBd_bore_2_readen = as_scalar<decltype(ref->boreChildrenBd_bore_2_readen)>(scalar);
        wolf->boreChildrenBd_bore_2_readen = as_scalar<decltype(wolf->boreChildrenBd_bore_2_readen)>(scalar);
        break;
    case 400:
        ref->boreChildrenBd_bore_2_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_2_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_2_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_2_addr_rd)>(scalar);
        break;
    case 401:
        ref->boreChildrenBd_bore_3_array = as_scalar<decltype(ref->boreChildrenBd_bore_3_array)>(scalar);
        wolf->boreChildrenBd_bore_3_array = as_scalar<decltype(wolf->boreChildrenBd_bore_3_array)>(scalar);
        break;
    case 402:
        ref->boreChildrenBd_bore_3_all = as_scalar<decltype(ref->boreChildrenBd_bore_3_all)>(scalar);
        wolf->boreChildrenBd_bore_3_all = as_scalar<decltype(wolf->boreChildrenBd_bore_3_all)>(scalar);
        break;
    case 403:
        ref->boreChildrenBd_bore_3_req = as_scalar<decltype(ref->boreChildrenBd_bore_3_req)>(scalar);
        wolf->boreChildrenBd_bore_3_req = as_scalar<decltype(wolf->boreChildrenBd_bore_3_req)>(scalar);
        break;
    case 404:
        ref->boreChildrenBd_bore_3_writeen = as_scalar<decltype(ref->boreChildrenBd_bore_3_writeen)>(scalar);
        wolf->boreChildrenBd_bore_3_writeen = as_scalar<decltype(wolf->boreChildrenBd_bore_3_writeen)>(scalar);
        break;
    case 405:
        for (int i = 0; i < 3; ++i) {
            ref->boreChildrenBd_bore_3_be.data()[i] = words[i];
            wolf->boreChildrenBd_bore_3_be.data()[i] = words[i];
        }
        break;
    case 406:
        ref->boreChildrenBd_bore_3_addr = as_scalar<decltype(ref->boreChildrenBd_bore_3_addr)>(scalar);
        wolf->boreChildrenBd_bore_3_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_3_addr)>(scalar);
        break;
    case 407:
        for (int i = 0; i < 3; ++i) {
            ref->boreChildrenBd_bore_3_indata.data()[i] = words[i];
            wolf->boreChildrenBd_bore_3_indata.data()[i] = words[i];
        }
        break;
    case 408:
        ref->boreChildrenBd_bore_3_readen = as_scalar<decltype(ref->boreChildrenBd_bore_3_readen)>(scalar);
        wolf->boreChildrenBd_bore_3_readen = as_scalar<decltype(wolf->boreChildrenBd_bore_3_readen)>(scalar);
        break;
    case 409:
        ref->boreChildrenBd_bore_3_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_3_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_3_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_3_addr_rd)>(scalar);
        break;
    case 410:
        ref->boreChildrenBd_bore_4_array = as_scalar<decltype(ref->boreChildrenBd_bore_4_array)>(scalar);
        wolf->boreChildrenBd_bore_4_array = as_scalar<decltype(wolf->boreChildrenBd_bore_4_array)>(scalar);
        break;
    case 411:
        ref->boreChildrenBd_bore_4_all = as_scalar<decltype(ref->boreChildrenBd_bore_4_all)>(scalar);
        wolf->boreChildrenBd_bore_4_all = as_scalar<decltype(wolf->boreChildrenBd_bore_4_all)>(scalar);
        break;
    case 412:
        ref->boreChildrenBd_bore_4_req = as_scalar<decltype(ref->boreChildrenBd_bore_4_req)>(scalar);
        wolf->boreChildrenBd_bore_4_req = as_scalar<decltype(wolf->boreChildrenBd_bore_4_req)>(scalar);
        break;
    case 413:
        ref->boreChildrenBd_bore_4_writeen = as_scalar<decltype(ref->boreChildrenBd_bore_4_writeen)>(scalar);
        wolf->boreChildrenBd_bore_4_writeen = as_scalar<decltype(wolf->boreChildrenBd_bore_4_writeen)>(scalar);
        break;
    case 414:
        for (int i = 0; i < 3; ++i) {
            ref->boreChildrenBd_bore_4_be.data()[i] = words[i];
            wolf->boreChildrenBd_bore_4_be.data()[i] = words[i];
        }
        break;
    case 415:
        ref->boreChildrenBd_bore_4_addr = as_scalar<decltype(ref->boreChildrenBd_bore_4_addr)>(scalar);
        wolf->boreChildrenBd_bore_4_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_4_addr)>(scalar);
        break;
    case 416:
        for (int i = 0; i < 3; ++i) {
            ref->boreChildrenBd_bore_4_indata.data()[i] = words[i];
            wolf->boreChildrenBd_bore_4_indata.data()[i] = words[i];
        }
        break;
    case 417:
        ref->boreChildrenBd_bore_4_readen = as_scalar<decltype(ref->boreChildrenBd_bore_4_readen)>(scalar);
        wolf->boreChildrenBd_bore_4_readen = as_scalar<decltype(wolf->boreChildrenBd_bore_4_readen)>(scalar);
        break;
    case 418:
        ref->boreChildrenBd_bore_4_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_4_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_4_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_4_addr_rd)>(scalar);
        break;
    case 419:
        ref->boreChildrenBd_bore_5_addr = as_scalar<decltype(ref->boreChildrenBd_bore_5_addr)>(scalar);
        wolf->boreChildrenBd_bore_5_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_5_addr)>(scalar);
        break;
    case 420:
        ref->boreChildrenBd_bore_5_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_5_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_5_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_5_addr_rd)>(scalar);
        break;
    case 421:
        ref->boreChildrenBd_bore_5_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_5_wdata)>(scalar);
        wolf->boreChildrenBd_bore_5_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_5_wdata)>(scalar);
        break;
    case 422:
        ref->boreChildrenBd_bore_5_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_5_wmask)>(scalar);
        wolf->boreChildrenBd_bore_5_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_5_wmask)>(scalar);
        break;
    case 423:
        ref->boreChildrenBd_bore_5_re = as_scalar<decltype(ref->boreChildrenBd_bore_5_re)>(scalar);
        wolf->boreChildrenBd_bore_5_re = as_scalar<decltype(wolf->boreChildrenBd_bore_5_re)>(scalar);
        break;
    case 424:
        ref->boreChildrenBd_bore_5_we = as_scalar<decltype(ref->boreChildrenBd_bore_5_we)>(scalar);
        wolf->boreChildrenBd_bore_5_we = as_scalar<decltype(wolf->boreChildrenBd_bore_5_we)>(scalar);
        break;
    case 425:
        ref->boreChildrenBd_bore_5_ack = as_scalar<decltype(ref->boreChildrenBd_bore_5_ack)>(scalar);
        wolf->boreChildrenBd_bore_5_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_5_ack)>(scalar);
        break;
    case 426:
        ref->boreChildrenBd_bore_5_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_5_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_5_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_5_selectedOH)>(scalar);
        break;
    case 427:
        ref->boreChildrenBd_bore_5_array = as_scalar<decltype(ref->boreChildrenBd_bore_5_array)>(scalar);
        wolf->boreChildrenBd_bore_5_array = as_scalar<decltype(wolf->boreChildrenBd_bore_5_array)>(scalar);
        break;
    case 428:
        ref->boreChildrenBd_bore_6_addr = as_scalar<decltype(ref->boreChildrenBd_bore_6_addr)>(scalar);
        wolf->boreChildrenBd_bore_6_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_6_addr)>(scalar);
        break;
    case 429:
        ref->boreChildrenBd_bore_6_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_6_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_6_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_6_addr_rd)>(scalar);
        break;
    case 430:
        ref->boreChildrenBd_bore_6_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_6_wdata)>(scalar);
        wolf->boreChildrenBd_bore_6_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_6_wdata)>(scalar);
        break;
    case 431:
        ref->boreChildrenBd_bore_6_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_6_wmask)>(scalar);
        wolf->boreChildrenBd_bore_6_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_6_wmask)>(scalar);
        break;
    case 432:
        ref->boreChildrenBd_bore_6_re = as_scalar<decltype(ref->boreChildrenBd_bore_6_re)>(scalar);
        wolf->boreChildrenBd_bore_6_re = as_scalar<decltype(wolf->boreChildrenBd_bore_6_re)>(scalar);
        break;
    case 433:
        ref->boreChildrenBd_bore_6_we = as_scalar<decltype(ref->boreChildrenBd_bore_6_we)>(scalar);
        wolf->boreChildrenBd_bore_6_we = as_scalar<decltype(wolf->boreChildrenBd_bore_6_we)>(scalar);
        break;
    case 434:
        ref->boreChildrenBd_bore_6_ack = as_scalar<decltype(ref->boreChildrenBd_bore_6_ack)>(scalar);
        wolf->boreChildrenBd_bore_6_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_6_ack)>(scalar);
        break;
    case 435:
        ref->boreChildrenBd_bore_6_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_6_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_6_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_6_selectedOH)>(scalar);
        break;
    case 436:
        ref->boreChildrenBd_bore_6_array = as_scalar<decltype(ref->boreChildrenBd_bore_6_array)>(scalar);
        wolf->boreChildrenBd_bore_6_array = as_scalar<decltype(wolf->boreChildrenBd_bore_6_array)>(scalar);
        break;
    case 437:
        ref->boreChildrenBd_bore_7_addr = as_scalar<decltype(ref->boreChildrenBd_bore_7_addr)>(scalar);
        wolf->boreChildrenBd_bore_7_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_7_addr)>(scalar);
        break;
    case 438:
        ref->boreChildrenBd_bore_7_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_7_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_7_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_7_addr_rd)>(scalar);
        break;
    case 439:
        ref->boreChildrenBd_bore_7_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_7_wdata)>(scalar);
        wolf->boreChildrenBd_bore_7_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_7_wdata)>(scalar);
        break;
    case 440:
        ref->boreChildrenBd_bore_7_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_7_wmask)>(scalar);
        wolf->boreChildrenBd_bore_7_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_7_wmask)>(scalar);
        break;
    case 441:
        ref->boreChildrenBd_bore_7_re = as_scalar<decltype(ref->boreChildrenBd_bore_7_re)>(scalar);
        wolf->boreChildrenBd_bore_7_re = as_scalar<decltype(wolf->boreChildrenBd_bore_7_re)>(scalar);
        break;
    case 442:
        ref->boreChildrenBd_bore_7_we = as_scalar<decltype(ref->boreChildrenBd_bore_7_we)>(scalar);
        wolf->boreChildrenBd_bore_7_we = as_scalar<decltype(wolf->boreChildrenBd_bore_7_we)>(scalar);
        break;
    case 443:
        ref->boreChildrenBd_bore_7_ack = as_scalar<decltype(ref->boreChildrenBd_bore_7_ack)>(scalar);
        wolf->boreChildrenBd_bore_7_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_7_ack)>(scalar);
        break;
    case 444:
        ref->boreChildrenBd_bore_7_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_7_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_7_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_7_selectedOH)>(scalar);
        break;
    case 445:
        ref->boreChildrenBd_bore_7_array = as_scalar<decltype(ref->boreChildrenBd_bore_7_array)>(scalar);
        wolf->boreChildrenBd_bore_7_array = as_scalar<decltype(wolf->boreChildrenBd_bore_7_array)>(scalar);
        break;
    case 446:
        ref->boreChildrenBd_bore_8_addr = as_scalar<decltype(ref->boreChildrenBd_bore_8_addr)>(scalar);
        wolf->boreChildrenBd_bore_8_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_8_addr)>(scalar);
        break;
    case 447:
        ref->boreChildrenBd_bore_8_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_8_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_8_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_8_addr_rd)>(scalar);
        break;
    case 448:
        ref->boreChildrenBd_bore_8_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_8_wdata)>(scalar);
        wolf->boreChildrenBd_bore_8_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_8_wdata)>(scalar);
        break;
    case 449:
        ref->boreChildrenBd_bore_8_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_8_wmask)>(scalar);
        wolf->boreChildrenBd_bore_8_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_8_wmask)>(scalar);
        break;
    case 450:
        ref->boreChildrenBd_bore_8_re = as_scalar<decltype(ref->boreChildrenBd_bore_8_re)>(scalar);
        wolf->boreChildrenBd_bore_8_re = as_scalar<decltype(wolf->boreChildrenBd_bore_8_re)>(scalar);
        break;
    case 451:
        ref->boreChildrenBd_bore_8_we = as_scalar<decltype(ref->boreChildrenBd_bore_8_we)>(scalar);
        wolf->boreChildrenBd_bore_8_we = as_scalar<decltype(wolf->boreChildrenBd_bore_8_we)>(scalar);
        break;
    case 452:
        ref->boreChildrenBd_bore_8_ack = as_scalar<decltype(ref->boreChildrenBd_bore_8_ack)>(scalar);
        wolf->boreChildrenBd_bore_8_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_8_ack)>(scalar);
        break;
    case 453:
        ref->boreChildrenBd_bore_8_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_8_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_8_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_8_selectedOH)>(scalar);
        break;
    case 454:
        ref->boreChildrenBd_bore_8_array = as_scalar<decltype(ref->boreChildrenBd_bore_8_array)>(scalar);
        wolf->boreChildrenBd_bore_8_array = as_scalar<decltype(wolf->boreChildrenBd_bore_8_array)>(scalar);
        break;
    case 455:
        ref->boreChildrenBd_bore_9_addr = as_scalar<decltype(ref->boreChildrenBd_bore_9_addr)>(scalar);
        wolf->boreChildrenBd_bore_9_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_9_addr)>(scalar);
        break;
    case 456:
        ref->boreChildrenBd_bore_9_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_9_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_9_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_9_addr_rd)>(scalar);
        break;
    case 457:
        ref->boreChildrenBd_bore_9_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_9_wdata)>(scalar);
        wolf->boreChildrenBd_bore_9_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_9_wdata)>(scalar);
        break;
    case 458:
        ref->boreChildrenBd_bore_9_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_9_wmask)>(scalar);
        wolf->boreChildrenBd_bore_9_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_9_wmask)>(scalar);
        break;
    case 459:
        ref->boreChildrenBd_bore_9_re = as_scalar<decltype(ref->boreChildrenBd_bore_9_re)>(scalar);
        wolf->boreChildrenBd_bore_9_re = as_scalar<decltype(wolf->boreChildrenBd_bore_9_re)>(scalar);
        break;
    case 460:
        ref->boreChildrenBd_bore_9_we = as_scalar<decltype(ref->boreChildrenBd_bore_9_we)>(scalar);
        wolf->boreChildrenBd_bore_9_we = as_scalar<decltype(wolf->boreChildrenBd_bore_9_we)>(scalar);
        break;
    case 461:
        ref->boreChildrenBd_bore_9_ack = as_scalar<decltype(ref->boreChildrenBd_bore_9_ack)>(scalar);
        wolf->boreChildrenBd_bore_9_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_9_ack)>(scalar);
        break;
    case 462:
        ref->boreChildrenBd_bore_9_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_9_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_9_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_9_selectedOH)>(scalar);
        break;
    case 463:
        ref->boreChildrenBd_bore_9_array = as_scalar<decltype(ref->boreChildrenBd_bore_9_array)>(scalar);
        wolf->boreChildrenBd_bore_9_array = as_scalar<decltype(wolf->boreChildrenBd_bore_9_array)>(scalar);
        break;
    case 464:
        ref->boreChildrenBd_bore_10_addr = as_scalar<decltype(ref->boreChildrenBd_bore_10_addr)>(scalar);
        wolf->boreChildrenBd_bore_10_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_10_addr)>(scalar);
        break;
    case 465:
        ref->boreChildrenBd_bore_10_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_10_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_10_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_10_addr_rd)>(scalar);
        break;
    case 466:
        ref->boreChildrenBd_bore_10_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_10_wdata)>(scalar);
        wolf->boreChildrenBd_bore_10_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_10_wdata)>(scalar);
        break;
    case 467:
        ref->boreChildrenBd_bore_10_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_10_wmask)>(scalar);
        wolf->boreChildrenBd_bore_10_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_10_wmask)>(scalar);
        break;
    case 468:
        ref->boreChildrenBd_bore_10_re = as_scalar<decltype(ref->boreChildrenBd_bore_10_re)>(scalar);
        wolf->boreChildrenBd_bore_10_re = as_scalar<decltype(wolf->boreChildrenBd_bore_10_re)>(scalar);
        break;
    case 469:
        ref->boreChildrenBd_bore_10_we = as_scalar<decltype(ref->boreChildrenBd_bore_10_we)>(scalar);
        wolf->boreChildrenBd_bore_10_we = as_scalar<decltype(wolf->boreChildrenBd_bore_10_we)>(scalar);
        break;
    case 470:
        ref->boreChildrenBd_bore_10_ack = as_scalar<decltype(ref->boreChildrenBd_bore_10_ack)>(scalar);
        wolf->boreChildrenBd_bore_10_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_10_ack)>(scalar);
        break;
    case 471:
        ref->boreChildrenBd_bore_10_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_10_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_10_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_10_selectedOH)>(scalar);
        break;
    case 472:
        ref->boreChildrenBd_bore_10_array = as_scalar<decltype(ref->boreChildrenBd_bore_10_array)>(scalar);
        wolf->boreChildrenBd_bore_10_array = as_scalar<decltype(wolf->boreChildrenBd_bore_10_array)>(scalar);
        break;
    case 473:
        ref->boreChildrenBd_bore_11_addr = as_scalar<decltype(ref->boreChildrenBd_bore_11_addr)>(scalar);
        wolf->boreChildrenBd_bore_11_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_11_addr)>(scalar);
        break;
    case 474:
        ref->boreChildrenBd_bore_11_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_11_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_11_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_11_addr_rd)>(scalar);
        break;
    case 475:
        ref->boreChildrenBd_bore_11_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_11_wdata)>(scalar);
        wolf->boreChildrenBd_bore_11_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_11_wdata)>(scalar);
        break;
    case 476:
        ref->boreChildrenBd_bore_11_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_11_wmask)>(scalar);
        wolf->boreChildrenBd_bore_11_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_11_wmask)>(scalar);
        break;
    case 477:
        ref->boreChildrenBd_bore_11_re = as_scalar<decltype(ref->boreChildrenBd_bore_11_re)>(scalar);
        wolf->boreChildrenBd_bore_11_re = as_scalar<decltype(wolf->boreChildrenBd_bore_11_re)>(scalar);
        break;
    case 478:
        ref->boreChildrenBd_bore_11_we = as_scalar<decltype(ref->boreChildrenBd_bore_11_we)>(scalar);
        wolf->boreChildrenBd_bore_11_we = as_scalar<decltype(wolf->boreChildrenBd_bore_11_we)>(scalar);
        break;
    case 479:
        ref->boreChildrenBd_bore_11_ack = as_scalar<decltype(ref->boreChildrenBd_bore_11_ack)>(scalar);
        wolf->boreChildrenBd_bore_11_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_11_ack)>(scalar);
        break;
    case 480:
        ref->boreChildrenBd_bore_11_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_11_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_11_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_11_selectedOH)>(scalar);
        break;
    case 481:
        ref->boreChildrenBd_bore_11_array = as_scalar<decltype(ref->boreChildrenBd_bore_11_array)>(scalar);
        wolf->boreChildrenBd_bore_11_array = as_scalar<decltype(wolf->boreChildrenBd_bore_11_array)>(scalar);
        break;
    case 482:
        ref->boreChildrenBd_bore_12_addr = as_scalar<decltype(ref->boreChildrenBd_bore_12_addr)>(scalar);
        wolf->boreChildrenBd_bore_12_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_12_addr)>(scalar);
        break;
    case 483:
        ref->boreChildrenBd_bore_12_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_12_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_12_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_12_addr_rd)>(scalar);
        break;
    case 484:
        ref->boreChildrenBd_bore_12_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_12_wdata)>(scalar);
        wolf->boreChildrenBd_bore_12_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_12_wdata)>(scalar);
        break;
    case 485:
        ref->boreChildrenBd_bore_12_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_12_wmask)>(scalar);
        wolf->boreChildrenBd_bore_12_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_12_wmask)>(scalar);
        break;
    case 486:
        ref->boreChildrenBd_bore_12_re = as_scalar<decltype(ref->boreChildrenBd_bore_12_re)>(scalar);
        wolf->boreChildrenBd_bore_12_re = as_scalar<decltype(wolf->boreChildrenBd_bore_12_re)>(scalar);
        break;
    case 487:
        ref->boreChildrenBd_bore_12_we = as_scalar<decltype(ref->boreChildrenBd_bore_12_we)>(scalar);
        wolf->boreChildrenBd_bore_12_we = as_scalar<decltype(wolf->boreChildrenBd_bore_12_we)>(scalar);
        break;
    case 488:
        ref->boreChildrenBd_bore_12_ack = as_scalar<decltype(ref->boreChildrenBd_bore_12_ack)>(scalar);
        wolf->boreChildrenBd_bore_12_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_12_ack)>(scalar);
        break;
    case 489:
        ref->boreChildrenBd_bore_12_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_12_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_12_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_12_selectedOH)>(scalar);
        break;
    case 490:
        ref->boreChildrenBd_bore_12_array = as_scalar<decltype(ref->boreChildrenBd_bore_12_array)>(scalar);
        wolf->boreChildrenBd_bore_12_array = as_scalar<decltype(wolf->boreChildrenBd_bore_12_array)>(scalar);
        break;
    case 491:
        ref->boreChildrenBd_bore_13_addr = as_scalar<decltype(ref->boreChildrenBd_bore_13_addr)>(scalar);
        wolf->boreChildrenBd_bore_13_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_13_addr)>(scalar);
        break;
    case 492:
        ref->boreChildrenBd_bore_13_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_13_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_13_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_13_addr_rd)>(scalar);
        break;
    case 493:
        ref->boreChildrenBd_bore_13_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_13_wdata)>(scalar);
        wolf->boreChildrenBd_bore_13_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_13_wdata)>(scalar);
        break;
    case 494:
        ref->boreChildrenBd_bore_13_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_13_wmask)>(scalar);
        wolf->boreChildrenBd_bore_13_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_13_wmask)>(scalar);
        break;
    case 495:
        ref->boreChildrenBd_bore_13_re = as_scalar<decltype(ref->boreChildrenBd_bore_13_re)>(scalar);
        wolf->boreChildrenBd_bore_13_re = as_scalar<decltype(wolf->boreChildrenBd_bore_13_re)>(scalar);
        break;
    case 496:
        ref->boreChildrenBd_bore_13_we = as_scalar<decltype(ref->boreChildrenBd_bore_13_we)>(scalar);
        wolf->boreChildrenBd_bore_13_we = as_scalar<decltype(wolf->boreChildrenBd_bore_13_we)>(scalar);
        break;
    case 497:
        ref->boreChildrenBd_bore_13_ack = as_scalar<decltype(ref->boreChildrenBd_bore_13_ack)>(scalar);
        wolf->boreChildrenBd_bore_13_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_13_ack)>(scalar);
        break;
    case 498:
        ref->boreChildrenBd_bore_13_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_13_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_13_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_13_selectedOH)>(scalar);
        break;
    case 499:
        ref->boreChildrenBd_bore_13_array = as_scalar<decltype(ref->boreChildrenBd_bore_13_array)>(scalar);
        wolf->boreChildrenBd_bore_13_array = as_scalar<decltype(wolf->boreChildrenBd_bore_13_array)>(scalar);
        break;
    case 500:
        ref->boreChildrenBd_bore_14_addr = as_scalar<decltype(ref->boreChildrenBd_bore_14_addr)>(scalar);
        wolf->boreChildrenBd_bore_14_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_14_addr)>(scalar);
        break;
    case 501:
        ref->boreChildrenBd_bore_14_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_14_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_14_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_14_addr_rd)>(scalar);
        break;
    case 502:
        ref->boreChildrenBd_bore_14_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_14_wdata)>(scalar);
        wolf->boreChildrenBd_bore_14_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_14_wdata)>(scalar);
        break;
    case 503:
        ref->boreChildrenBd_bore_14_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_14_wmask)>(scalar);
        wolf->boreChildrenBd_bore_14_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_14_wmask)>(scalar);
        break;
    case 504:
        ref->boreChildrenBd_bore_14_re = as_scalar<decltype(ref->boreChildrenBd_bore_14_re)>(scalar);
        wolf->boreChildrenBd_bore_14_re = as_scalar<decltype(wolf->boreChildrenBd_bore_14_re)>(scalar);
        break;
    case 505:
        ref->boreChildrenBd_bore_14_we = as_scalar<decltype(ref->boreChildrenBd_bore_14_we)>(scalar);
        wolf->boreChildrenBd_bore_14_we = as_scalar<decltype(wolf->boreChildrenBd_bore_14_we)>(scalar);
        break;
    case 506:
        ref->boreChildrenBd_bore_14_ack = as_scalar<decltype(ref->boreChildrenBd_bore_14_ack)>(scalar);
        wolf->boreChildrenBd_bore_14_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_14_ack)>(scalar);
        break;
    case 507:
        ref->boreChildrenBd_bore_14_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_14_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_14_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_14_selectedOH)>(scalar);
        break;
    case 508:
        ref->boreChildrenBd_bore_14_array = as_scalar<decltype(ref->boreChildrenBd_bore_14_array)>(scalar);
        wolf->boreChildrenBd_bore_14_array = as_scalar<decltype(wolf->boreChildrenBd_bore_14_array)>(scalar);
        break;
    case 509:
        ref->boreChildrenBd_bore_15_addr = as_scalar<decltype(ref->boreChildrenBd_bore_15_addr)>(scalar);
        wolf->boreChildrenBd_bore_15_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_15_addr)>(scalar);
        break;
    case 510:
        ref->boreChildrenBd_bore_15_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_15_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_15_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_15_addr_rd)>(scalar);
        break;
    case 511:
        ref->boreChildrenBd_bore_15_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_15_wdata)>(scalar);
        wolf->boreChildrenBd_bore_15_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_15_wdata)>(scalar);
        break;
    case 512:
        ref->boreChildrenBd_bore_15_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_15_wmask)>(scalar);
        wolf->boreChildrenBd_bore_15_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_15_wmask)>(scalar);
        break;
    case 513:
        ref->boreChildrenBd_bore_15_re = as_scalar<decltype(ref->boreChildrenBd_bore_15_re)>(scalar);
        wolf->boreChildrenBd_bore_15_re = as_scalar<decltype(wolf->boreChildrenBd_bore_15_re)>(scalar);
        break;
    case 514:
        ref->boreChildrenBd_bore_15_we = as_scalar<decltype(ref->boreChildrenBd_bore_15_we)>(scalar);
        wolf->boreChildrenBd_bore_15_we = as_scalar<decltype(wolf->boreChildrenBd_bore_15_we)>(scalar);
        break;
    case 515:
        ref->boreChildrenBd_bore_15_ack = as_scalar<decltype(ref->boreChildrenBd_bore_15_ack)>(scalar);
        wolf->boreChildrenBd_bore_15_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_15_ack)>(scalar);
        break;
    case 516:
        ref->boreChildrenBd_bore_15_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_15_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_15_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_15_selectedOH)>(scalar);
        break;
    case 517:
        ref->boreChildrenBd_bore_15_array = as_scalar<decltype(ref->boreChildrenBd_bore_15_array)>(scalar);
        wolf->boreChildrenBd_bore_15_array = as_scalar<decltype(wolf->boreChildrenBd_bore_15_array)>(scalar);
        break;
    case 518:
        ref->boreChildrenBd_bore_16_addr = as_scalar<decltype(ref->boreChildrenBd_bore_16_addr)>(scalar);
        wolf->boreChildrenBd_bore_16_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_16_addr)>(scalar);
        break;
    case 519:
        ref->boreChildrenBd_bore_16_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_16_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_16_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_16_addr_rd)>(scalar);
        break;
    case 520:
        ref->boreChildrenBd_bore_16_wdata = as_scalar<decltype(ref->boreChildrenBd_bore_16_wdata)>(scalar);
        wolf->boreChildrenBd_bore_16_wdata = as_scalar<decltype(wolf->boreChildrenBd_bore_16_wdata)>(scalar);
        break;
    case 521:
        ref->boreChildrenBd_bore_16_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_16_wmask)>(scalar);
        wolf->boreChildrenBd_bore_16_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_16_wmask)>(scalar);
        break;
    case 522:
        ref->boreChildrenBd_bore_16_re = as_scalar<decltype(ref->boreChildrenBd_bore_16_re)>(scalar);
        wolf->boreChildrenBd_bore_16_re = as_scalar<decltype(wolf->boreChildrenBd_bore_16_re)>(scalar);
        break;
    case 523:
        ref->boreChildrenBd_bore_16_we = as_scalar<decltype(ref->boreChildrenBd_bore_16_we)>(scalar);
        wolf->boreChildrenBd_bore_16_we = as_scalar<decltype(wolf->boreChildrenBd_bore_16_we)>(scalar);
        break;
    case 524:
        ref->boreChildrenBd_bore_16_ack = as_scalar<decltype(ref->boreChildrenBd_bore_16_ack)>(scalar);
        wolf->boreChildrenBd_bore_16_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_16_ack)>(scalar);
        break;
    case 525:
        ref->boreChildrenBd_bore_16_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_16_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_16_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_16_selectedOH)>(scalar);
        break;
    case 526:
        ref->boreChildrenBd_bore_16_array = as_scalar<decltype(ref->boreChildrenBd_bore_16_array)>(scalar);
        wolf->boreChildrenBd_bore_16_array = as_scalar<decltype(wolf->boreChildrenBd_bore_16_array)>(scalar);
        break;
    case 527:
        ref->boreChildrenBd_bore_17_addr = as_scalar<decltype(ref->boreChildrenBd_bore_17_addr)>(scalar);
        wolf->boreChildrenBd_bore_17_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_17_addr)>(scalar);
        break;
    case 528:
        ref->boreChildrenBd_bore_17_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_17_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_17_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_17_addr_rd)>(scalar);
        break;
    case 529:
        for (int i = 0; i < 6; ++i) {
            ref->boreChildrenBd_bore_17_wdata.data()[i] = words[i];
            wolf->boreChildrenBd_bore_17_wdata.data()[i] = words[i];
        }
        break;
    case 530:
        ref->boreChildrenBd_bore_17_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_17_wmask)>(scalar);
        wolf->boreChildrenBd_bore_17_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_17_wmask)>(scalar);
        break;
    case 531:
        ref->boreChildrenBd_bore_17_re = as_scalar<decltype(ref->boreChildrenBd_bore_17_re)>(scalar);
        wolf->boreChildrenBd_bore_17_re = as_scalar<decltype(wolf->boreChildrenBd_bore_17_re)>(scalar);
        break;
    case 532:
        ref->boreChildrenBd_bore_17_we = as_scalar<decltype(ref->boreChildrenBd_bore_17_we)>(scalar);
        wolf->boreChildrenBd_bore_17_we = as_scalar<decltype(wolf->boreChildrenBd_bore_17_we)>(scalar);
        break;
    case 533:
        ref->boreChildrenBd_bore_17_ack = as_scalar<decltype(ref->boreChildrenBd_bore_17_ack)>(scalar);
        wolf->boreChildrenBd_bore_17_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_17_ack)>(scalar);
        break;
    case 534:
        ref->boreChildrenBd_bore_17_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_17_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_17_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_17_selectedOH)>(scalar);
        break;
    case 535:
        ref->boreChildrenBd_bore_17_array = as_scalar<decltype(ref->boreChildrenBd_bore_17_array)>(scalar);
        wolf->boreChildrenBd_bore_17_array = as_scalar<decltype(wolf->boreChildrenBd_bore_17_array)>(scalar);
        break;
    case 536:
        ref->boreChildrenBd_bore_18_addr = as_scalar<decltype(ref->boreChildrenBd_bore_18_addr)>(scalar);
        wolf->boreChildrenBd_bore_18_addr = as_scalar<decltype(wolf->boreChildrenBd_bore_18_addr)>(scalar);
        break;
    case 537:
        ref->boreChildrenBd_bore_18_addr_rd = as_scalar<decltype(ref->boreChildrenBd_bore_18_addr_rd)>(scalar);
        wolf->boreChildrenBd_bore_18_addr_rd = as_scalar<decltype(wolf->boreChildrenBd_bore_18_addr_rd)>(scalar);
        break;
    case 538:
        for (int i = 0; i < 6; ++i) {
            ref->boreChildrenBd_bore_18_wdata.data()[i] = words[i];
            wolf->boreChildrenBd_bore_18_wdata.data()[i] = words[i];
        }
        break;
    case 539:
        ref->boreChildrenBd_bore_18_wmask = as_scalar<decltype(ref->boreChildrenBd_bore_18_wmask)>(scalar);
        wolf->boreChildrenBd_bore_18_wmask = as_scalar<decltype(wolf->boreChildrenBd_bore_18_wmask)>(scalar);
        break;
    case 540:
        ref->boreChildrenBd_bore_18_re = as_scalar<decltype(ref->boreChildrenBd_bore_18_re)>(scalar);
        wolf->boreChildrenBd_bore_18_re = as_scalar<decltype(wolf->boreChildrenBd_bore_18_re)>(scalar);
        break;
    case 541:
        ref->boreChildrenBd_bore_18_we = as_scalar<decltype(ref->boreChildrenBd_bore_18_we)>(scalar);
        wolf->boreChildrenBd_bore_18_we = as_scalar<decltype(wolf->boreChildrenBd_bore_18_we)>(scalar);
        break;
    case 542:
        ref->boreChildrenBd_bore_18_ack = as_scalar<decltype(ref->boreChildrenBd_bore_18_ack)>(scalar);
        wolf->boreChildrenBd_bore_18_ack = as_scalar<decltype(wolf->boreChildrenBd_bore_18_ack)>(scalar);
        break;
    case 543:
        ref->boreChildrenBd_bore_18_selectedOH = as_scalar<decltype(ref->boreChildrenBd_bore_18_selectedOH)>(scalar);
        wolf->boreChildrenBd_bore_18_selectedOH = as_scalar<decltype(wolf->boreChildrenBd_bore_18_selectedOH)>(scalar);
        break;
    case 544:
        ref->boreChildrenBd_bore_18_array = as_scalar<decltype(ref->boreChildrenBd_bore_18_array)>(scalar);
        wolf->boreChildrenBd_bore_18_array = as_scalar<decltype(wolf->boreChildrenBd_bore_18_array)>(scalar);
        break;
    case 545:
        ref->sigFromSrams_bore_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_ram_hold)>(scalar);
        break;
    case 546:
        ref->sigFromSrams_bore_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_ram_bypass)>(scalar);
        break;
    case 547:
        ref->sigFromSrams_bore_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_ram_bp_clken)>(scalar);
        break;
    case 548:
        ref->sigFromSrams_bore_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_ram_aux_clk)>(scalar);
        break;
    case 549:
        ref->sigFromSrams_bore_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_ram_aux_ckbp)>(scalar);
        break;
    case 550:
        ref->sigFromSrams_bore_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_ram_mcp_hold)>(scalar);
        break;
    case 551:
        ref->sigFromSrams_bore_cgen = as_scalar<decltype(ref->sigFromSrams_bore_cgen)>(scalar);
        wolf->sigFromSrams_bore_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_cgen)>(scalar);
        break;
    case 552:
        ref->sigFromSrams_bore_1_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_1_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_1_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_1_ram_hold)>(scalar);
        break;
    case 553:
        ref->sigFromSrams_bore_1_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_1_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_1_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_1_ram_bypass)>(scalar);
        break;
    case 554:
        ref->sigFromSrams_bore_1_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_1_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_1_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_1_ram_bp_clken)>(scalar);
        break;
    case 555:
        ref->sigFromSrams_bore_1_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_1_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_1_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_1_ram_aux_clk)>(scalar);
        break;
    case 556:
        ref->sigFromSrams_bore_1_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_1_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_1_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_1_ram_aux_ckbp)>(scalar);
        break;
    case 557:
        ref->sigFromSrams_bore_1_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_1_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_1_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_1_ram_mcp_hold)>(scalar);
        break;
    case 558:
        ref->sigFromSrams_bore_1_cgen = as_scalar<decltype(ref->sigFromSrams_bore_1_cgen)>(scalar);
        wolf->sigFromSrams_bore_1_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_1_cgen)>(scalar);
        break;
    case 559:
        ref->sigFromSrams_bore_2_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_2_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_2_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_2_ram_hold)>(scalar);
        break;
    case 560:
        ref->sigFromSrams_bore_2_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_2_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_2_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_2_ram_bypass)>(scalar);
        break;
    case 561:
        ref->sigFromSrams_bore_2_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_2_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_2_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_2_ram_bp_clken)>(scalar);
        break;
    case 562:
        ref->sigFromSrams_bore_2_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_2_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_2_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_2_ram_aux_clk)>(scalar);
        break;
    case 563:
        ref->sigFromSrams_bore_2_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_2_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_2_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_2_ram_aux_ckbp)>(scalar);
        break;
    case 564:
        ref->sigFromSrams_bore_2_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_2_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_2_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_2_ram_mcp_hold)>(scalar);
        break;
    case 565:
        ref->sigFromSrams_bore_2_cgen = as_scalar<decltype(ref->sigFromSrams_bore_2_cgen)>(scalar);
        wolf->sigFromSrams_bore_2_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_2_cgen)>(scalar);
        break;
    case 566:
        ref->sigFromSrams_bore_3_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_3_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_3_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_3_ram_hold)>(scalar);
        break;
    case 567:
        ref->sigFromSrams_bore_3_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_3_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_3_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_3_ram_bypass)>(scalar);
        break;
    case 568:
        ref->sigFromSrams_bore_3_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_3_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_3_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_3_ram_bp_clken)>(scalar);
        break;
    case 569:
        ref->sigFromSrams_bore_3_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_3_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_3_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_3_ram_aux_clk)>(scalar);
        break;
    case 570:
        ref->sigFromSrams_bore_3_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_3_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_3_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_3_ram_aux_ckbp)>(scalar);
        break;
    case 571:
        ref->sigFromSrams_bore_3_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_3_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_3_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_3_ram_mcp_hold)>(scalar);
        break;
    case 572:
        ref->sigFromSrams_bore_3_cgen = as_scalar<decltype(ref->sigFromSrams_bore_3_cgen)>(scalar);
        wolf->sigFromSrams_bore_3_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_3_cgen)>(scalar);
        break;
    case 573:
        ref->sigFromSrams_bore_4_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_4_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_4_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_4_ram_hold)>(scalar);
        break;
    case 574:
        ref->sigFromSrams_bore_4_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_4_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_4_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_4_ram_bypass)>(scalar);
        break;
    case 575:
        ref->sigFromSrams_bore_4_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_4_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_4_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_4_ram_bp_clken)>(scalar);
        break;
    case 576:
        ref->sigFromSrams_bore_4_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_4_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_4_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_4_ram_aux_clk)>(scalar);
        break;
    case 577:
        ref->sigFromSrams_bore_4_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_4_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_4_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_4_ram_aux_ckbp)>(scalar);
        break;
    case 578:
        ref->sigFromSrams_bore_4_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_4_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_4_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_4_ram_mcp_hold)>(scalar);
        break;
    case 579:
        ref->sigFromSrams_bore_4_cgen = as_scalar<decltype(ref->sigFromSrams_bore_4_cgen)>(scalar);
        wolf->sigFromSrams_bore_4_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_4_cgen)>(scalar);
        break;
    case 580:
        ref->sigFromSrams_bore_5_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_5_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_5_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_5_ram_hold)>(scalar);
        break;
    case 581:
        ref->sigFromSrams_bore_5_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_5_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_5_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_5_ram_bypass)>(scalar);
        break;
    case 582:
        ref->sigFromSrams_bore_5_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_5_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_5_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_5_ram_bp_clken)>(scalar);
        break;
    case 583:
        ref->sigFromSrams_bore_5_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_5_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_5_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_5_ram_aux_clk)>(scalar);
        break;
    case 584:
        ref->sigFromSrams_bore_5_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_5_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_5_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_5_ram_aux_ckbp)>(scalar);
        break;
    case 585:
        ref->sigFromSrams_bore_5_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_5_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_5_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_5_ram_mcp_hold)>(scalar);
        break;
    case 586:
        ref->sigFromSrams_bore_5_cgen = as_scalar<decltype(ref->sigFromSrams_bore_5_cgen)>(scalar);
        wolf->sigFromSrams_bore_5_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_5_cgen)>(scalar);
        break;
    case 587:
        ref->sigFromSrams_bore_6_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_6_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_6_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_6_ram_hold)>(scalar);
        break;
    case 588:
        ref->sigFromSrams_bore_6_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_6_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_6_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_6_ram_bypass)>(scalar);
        break;
    case 589:
        ref->sigFromSrams_bore_6_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_6_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_6_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_6_ram_bp_clken)>(scalar);
        break;
    case 590:
        ref->sigFromSrams_bore_6_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_6_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_6_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_6_ram_aux_clk)>(scalar);
        break;
    case 591:
        ref->sigFromSrams_bore_6_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_6_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_6_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_6_ram_aux_ckbp)>(scalar);
        break;
    case 592:
        ref->sigFromSrams_bore_6_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_6_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_6_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_6_ram_mcp_hold)>(scalar);
        break;
    case 593:
        ref->sigFromSrams_bore_6_cgen = as_scalar<decltype(ref->sigFromSrams_bore_6_cgen)>(scalar);
        wolf->sigFromSrams_bore_6_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_6_cgen)>(scalar);
        break;
    case 594:
        ref->sigFromSrams_bore_7_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_7_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_7_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_7_ram_hold)>(scalar);
        break;
    case 595:
        ref->sigFromSrams_bore_7_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_7_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_7_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_7_ram_bypass)>(scalar);
        break;
    case 596:
        ref->sigFromSrams_bore_7_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_7_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_7_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_7_ram_bp_clken)>(scalar);
        break;
    case 597:
        ref->sigFromSrams_bore_7_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_7_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_7_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_7_ram_aux_clk)>(scalar);
        break;
    case 598:
        ref->sigFromSrams_bore_7_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_7_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_7_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_7_ram_aux_ckbp)>(scalar);
        break;
    case 599:
        ref->sigFromSrams_bore_7_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_7_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_7_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_7_ram_mcp_hold)>(scalar);
        break;
    case 600:
        ref->sigFromSrams_bore_7_cgen = as_scalar<decltype(ref->sigFromSrams_bore_7_cgen)>(scalar);
        wolf->sigFromSrams_bore_7_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_7_cgen)>(scalar);
        break;
    case 601:
        ref->sigFromSrams_bore_8_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_8_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_8_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_8_ram_hold)>(scalar);
        break;
    case 602:
        ref->sigFromSrams_bore_8_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_8_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_8_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_8_ram_bypass)>(scalar);
        break;
    case 603:
        ref->sigFromSrams_bore_8_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_8_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_8_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_8_ram_bp_clken)>(scalar);
        break;
    case 604:
        ref->sigFromSrams_bore_8_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_8_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_8_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_8_ram_aux_clk)>(scalar);
        break;
    case 605:
        ref->sigFromSrams_bore_8_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_8_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_8_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_8_ram_aux_ckbp)>(scalar);
        break;
    case 606:
        ref->sigFromSrams_bore_8_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_8_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_8_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_8_ram_mcp_hold)>(scalar);
        break;
    case 607:
        ref->sigFromSrams_bore_8_cgen = as_scalar<decltype(ref->sigFromSrams_bore_8_cgen)>(scalar);
        wolf->sigFromSrams_bore_8_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_8_cgen)>(scalar);
        break;
    case 608:
        ref->sigFromSrams_bore_9_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_9_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_9_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_9_ram_hold)>(scalar);
        break;
    case 609:
        ref->sigFromSrams_bore_9_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_9_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_9_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_9_ram_bypass)>(scalar);
        break;
    case 610:
        ref->sigFromSrams_bore_9_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_9_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_9_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_9_ram_bp_clken)>(scalar);
        break;
    case 611:
        ref->sigFromSrams_bore_9_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_9_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_9_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_9_ram_aux_clk)>(scalar);
        break;
    case 612:
        ref->sigFromSrams_bore_9_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_9_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_9_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_9_ram_aux_ckbp)>(scalar);
        break;
    case 613:
        ref->sigFromSrams_bore_9_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_9_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_9_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_9_ram_mcp_hold)>(scalar);
        break;
    case 614:
        ref->sigFromSrams_bore_9_cgen = as_scalar<decltype(ref->sigFromSrams_bore_9_cgen)>(scalar);
        wolf->sigFromSrams_bore_9_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_9_cgen)>(scalar);
        break;
    case 615:
        ref->sigFromSrams_bore_10_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_10_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_10_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_10_ram_hold)>(scalar);
        break;
    case 616:
        ref->sigFromSrams_bore_10_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_10_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_10_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_10_ram_bypass)>(scalar);
        break;
    case 617:
        ref->sigFromSrams_bore_10_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_10_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_10_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_10_ram_bp_clken)>(scalar);
        break;
    case 618:
        ref->sigFromSrams_bore_10_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_10_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_10_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_10_ram_aux_clk)>(scalar);
        break;
    case 619:
        ref->sigFromSrams_bore_10_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_10_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_10_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_10_ram_aux_ckbp)>(scalar);
        break;
    case 620:
        ref->sigFromSrams_bore_10_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_10_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_10_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_10_ram_mcp_hold)>(scalar);
        break;
    case 621:
        ref->sigFromSrams_bore_10_cgen = as_scalar<decltype(ref->sigFromSrams_bore_10_cgen)>(scalar);
        wolf->sigFromSrams_bore_10_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_10_cgen)>(scalar);
        break;
    case 622:
        ref->sigFromSrams_bore_11_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_11_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_11_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_11_ram_hold)>(scalar);
        break;
    case 623:
        ref->sigFromSrams_bore_11_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_11_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_11_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_11_ram_bypass)>(scalar);
        break;
    case 624:
        ref->sigFromSrams_bore_11_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_11_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_11_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_11_ram_bp_clken)>(scalar);
        break;
    case 625:
        ref->sigFromSrams_bore_11_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_11_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_11_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_11_ram_aux_clk)>(scalar);
        break;
    case 626:
        ref->sigFromSrams_bore_11_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_11_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_11_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_11_ram_aux_ckbp)>(scalar);
        break;
    case 627:
        ref->sigFromSrams_bore_11_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_11_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_11_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_11_ram_mcp_hold)>(scalar);
        break;
    case 628:
        ref->sigFromSrams_bore_11_cgen = as_scalar<decltype(ref->sigFromSrams_bore_11_cgen)>(scalar);
        wolf->sigFromSrams_bore_11_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_11_cgen)>(scalar);
        break;
    case 629:
        ref->sigFromSrams_bore_12_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_12_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_12_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_12_ram_hold)>(scalar);
        break;
    case 630:
        ref->sigFromSrams_bore_12_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_12_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_12_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_12_ram_bypass)>(scalar);
        break;
    case 631:
        ref->sigFromSrams_bore_12_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_12_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_12_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_12_ram_bp_clken)>(scalar);
        break;
    case 632:
        ref->sigFromSrams_bore_12_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_12_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_12_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_12_ram_aux_clk)>(scalar);
        break;
    case 633:
        ref->sigFromSrams_bore_12_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_12_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_12_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_12_ram_aux_ckbp)>(scalar);
        break;
    case 634:
        ref->sigFromSrams_bore_12_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_12_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_12_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_12_ram_mcp_hold)>(scalar);
        break;
    case 635:
        ref->sigFromSrams_bore_12_cgen = as_scalar<decltype(ref->sigFromSrams_bore_12_cgen)>(scalar);
        wolf->sigFromSrams_bore_12_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_12_cgen)>(scalar);
        break;
    case 636:
        ref->sigFromSrams_bore_13_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_13_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_13_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_13_ram_hold)>(scalar);
        break;
    case 637:
        ref->sigFromSrams_bore_13_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_13_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_13_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_13_ram_bypass)>(scalar);
        break;
    case 638:
        ref->sigFromSrams_bore_13_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_13_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_13_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_13_ram_bp_clken)>(scalar);
        break;
    case 639:
        ref->sigFromSrams_bore_13_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_13_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_13_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_13_ram_aux_clk)>(scalar);
        break;
    case 640:
        ref->sigFromSrams_bore_13_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_13_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_13_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_13_ram_aux_ckbp)>(scalar);
        break;
    case 641:
        ref->sigFromSrams_bore_13_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_13_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_13_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_13_ram_mcp_hold)>(scalar);
        break;
    case 642:
        ref->sigFromSrams_bore_13_cgen = as_scalar<decltype(ref->sigFromSrams_bore_13_cgen)>(scalar);
        wolf->sigFromSrams_bore_13_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_13_cgen)>(scalar);
        break;
    case 643:
        ref->sigFromSrams_bore_14_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_14_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_14_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_14_ram_hold)>(scalar);
        break;
    case 644:
        ref->sigFromSrams_bore_14_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_14_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_14_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_14_ram_bypass)>(scalar);
        break;
    case 645:
        ref->sigFromSrams_bore_14_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_14_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_14_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_14_ram_bp_clken)>(scalar);
        break;
    case 646:
        ref->sigFromSrams_bore_14_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_14_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_14_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_14_ram_aux_clk)>(scalar);
        break;
    case 647:
        ref->sigFromSrams_bore_14_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_14_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_14_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_14_ram_aux_ckbp)>(scalar);
        break;
    case 648:
        ref->sigFromSrams_bore_14_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_14_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_14_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_14_ram_mcp_hold)>(scalar);
        break;
    case 649:
        ref->sigFromSrams_bore_14_cgen = as_scalar<decltype(ref->sigFromSrams_bore_14_cgen)>(scalar);
        wolf->sigFromSrams_bore_14_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_14_cgen)>(scalar);
        break;
    case 650:
        ref->sigFromSrams_bore_15_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_15_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_15_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_15_ram_hold)>(scalar);
        break;
    case 651:
        ref->sigFromSrams_bore_15_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_15_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_15_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_15_ram_bypass)>(scalar);
        break;
    case 652:
        ref->sigFromSrams_bore_15_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_15_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_15_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_15_ram_bp_clken)>(scalar);
        break;
    case 653:
        ref->sigFromSrams_bore_15_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_15_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_15_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_15_ram_aux_clk)>(scalar);
        break;
    case 654:
        ref->sigFromSrams_bore_15_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_15_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_15_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_15_ram_aux_ckbp)>(scalar);
        break;
    case 655:
        ref->sigFromSrams_bore_15_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_15_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_15_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_15_ram_mcp_hold)>(scalar);
        break;
    case 656:
        ref->sigFromSrams_bore_15_cgen = as_scalar<decltype(ref->sigFromSrams_bore_15_cgen)>(scalar);
        wolf->sigFromSrams_bore_15_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_15_cgen)>(scalar);
        break;
    case 657:
        ref->sigFromSrams_bore_16_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_16_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_16_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_16_ram_hold)>(scalar);
        break;
    case 658:
        ref->sigFromSrams_bore_16_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_16_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_16_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_16_ram_bypass)>(scalar);
        break;
    case 659:
        ref->sigFromSrams_bore_16_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_16_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_16_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_16_ram_bp_clken)>(scalar);
        break;
    case 660:
        ref->sigFromSrams_bore_16_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_16_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_16_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_16_ram_aux_clk)>(scalar);
        break;
    case 661:
        ref->sigFromSrams_bore_16_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_16_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_16_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_16_ram_aux_ckbp)>(scalar);
        break;
    case 662:
        ref->sigFromSrams_bore_16_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_16_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_16_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_16_ram_mcp_hold)>(scalar);
        break;
    case 663:
        ref->sigFromSrams_bore_16_cgen = as_scalar<decltype(ref->sigFromSrams_bore_16_cgen)>(scalar);
        wolf->sigFromSrams_bore_16_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_16_cgen)>(scalar);
        break;
    case 664:
        ref->sigFromSrams_bore_17_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_17_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_17_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_17_ram_hold)>(scalar);
        break;
    case 665:
        ref->sigFromSrams_bore_17_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_17_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_17_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_17_ram_bypass)>(scalar);
        break;
    case 666:
        ref->sigFromSrams_bore_17_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_17_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_17_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_17_ram_bp_clken)>(scalar);
        break;
    case 667:
        ref->sigFromSrams_bore_17_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_17_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_17_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_17_ram_aux_clk)>(scalar);
        break;
    case 668:
        ref->sigFromSrams_bore_17_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_17_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_17_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_17_ram_aux_ckbp)>(scalar);
        break;
    case 669:
        ref->sigFromSrams_bore_17_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_17_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_17_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_17_ram_mcp_hold)>(scalar);
        break;
    case 670:
        ref->sigFromSrams_bore_17_cgen = as_scalar<decltype(ref->sigFromSrams_bore_17_cgen)>(scalar);
        wolf->sigFromSrams_bore_17_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_17_cgen)>(scalar);
        break;
    case 671:
        ref->sigFromSrams_bore_18_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_18_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_18_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_18_ram_hold)>(scalar);
        break;
    case 672:
        ref->sigFromSrams_bore_18_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_18_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_18_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_18_ram_bypass)>(scalar);
        break;
    case 673:
        ref->sigFromSrams_bore_18_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_18_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_18_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_18_ram_bp_clken)>(scalar);
        break;
    case 674:
        ref->sigFromSrams_bore_18_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_18_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_18_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_18_ram_aux_clk)>(scalar);
        break;
    case 675:
        ref->sigFromSrams_bore_18_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_18_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_18_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_18_ram_aux_ckbp)>(scalar);
        break;
    case 676:
        ref->sigFromSrams_bore_18_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_18_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_18_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_18_ram_mcp_hold)>(scalar);
        break;
    case 677:
        ref->sigFromSrams_bore_18_cgen = as_scalar<decltype(ref->sigFromSrams_bore_18_cgen)>(scalar);
        wolf->sigFromSrams_bore_18_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_18_cgen)>(scalar);
        break;
    case 678:
        ref->sigFromSrams_bore_19_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_19_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_19_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_19_ram_hold)>(scalar);
        break;
    case 679:
        ref->sigFromSrams_bore_19_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_19_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_19_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_19_ram_bypass)>(scalar);
        break;
    case 680:
        ref->sigFromSrams_bore_19_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_19_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_19_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_19_ram_bp_clken)>(scalar);
        break;
    case 681:
        ref->sigFromSrams_bore_19_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_19_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_19_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_19_ram_aux_clk)>(scalar);
        break;
    case 682:
        ref->sigFromSrams_bore_19_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_19_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_19_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_19_ram_aux_ckbp)>(scalar);
        break;
    case 683:
        ref->sigFromSrams_bore_19_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_19_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_19_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_19_ram_mcp_hold)>(scalar);
        break;
    case 684:
        ref->sigFromSrams_bore_19_cgen = as_scalar<decltype(ref->sigFromSrams_bore_19_cgen)>(scalar);
        wolf->sigFromSrams_bore_19_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_19_cgen)>(scalar);
        break;
    case 685:
        ref->sigFromSrams_bore_20_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_20_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_20_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_20_ram_hold)>(scalar);
        break;
    case 686:
        ref->sigFromSrams_bore_20_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_20_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_20_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_20_ram_bypass)>(scalar);
        break;
    case 687:
        ref->sigFromSrams_bore_20_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_20_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_20_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_20_ram_bp_clken)>(scalar);
        break;
    case 688:
        ref->sigFromSrams_bore_20_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_20_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_20_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_20_ram_aux_clk)>(scalar);
        break;
    case 689:
        ref->sigFromSrams_bore_20_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_20_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_20_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_20_ram_aux_ckbp)>(scalar);
        break;
    case 690:
        ref->sigFromSrams_bore_20_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_20_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_20_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_20_ram_mcp_hold)>(scalar);
        break;
    case 691:
        ref->sigFromSrams_bore_20_cgen = as_scalar<decltype(ref->sigFromSrams_bore_20_cgen)>(scalar);
        wolf->sigFromSrams_bore_20_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_20_cgen)>(scalar);
        break;
    case 692:
        ref->sigFromSrams_bore_21_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_21_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_21_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_21_ram_hold)>(scalar);
        break;
    case 693:
        ref->sigFromSrams_bore_21_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_21_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_21_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_21_ram_bypass)>(scalar);
        break;
    case 694:
        ref->sigFromSrams_bore_21_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_21_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_21_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_21_ram_bp_clken)>(scalar);
        break;
    case 695:
        ref->sigFromSrams_bore_21_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_21_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_21_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_21_ram_aux_clk)>(scalar);
        break;
    case 696:
        ref->sigFromSrams_bore_21_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_21_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_21_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_21_ram_aux_ckbp)>(scalar);
        break;
    case 697:
        ref->sigFromSrams_bore_21_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_21_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_21_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_21_ram_mcp_hold)>(scalar);
        break;
    case 698:
        ref->sigFromSrams_bore_21_cgen = as_scalar<decltype(ref->sigFromSrams_bore_21_cgen)>(scalar);
        wolf->sigFromSrams_bore_21_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_21_cgen)>(scalar);
        break;
    case 699:
        ref->sigFromSrams_bore_22_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_22_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_22_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_22_ram_hold)>(scalar);
        break;
    case 700:
        ref->sigFromSrams_bore_22_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_22_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_22_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_22_ram_bypass)>(scalar);
        break;
    case 701:
        ref->sigFromSrams_bore_22_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_22_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_22_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_22_ram_bp_clken)>(scalar);
        break;
    case 702:
        ref->sigFromSrams_bore_22_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_22_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_22_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_22_ram_aux_clk)>(scalar);
        break;
    case 703:
        ref->sigFromSrams_bore_22_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_22_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_22_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_22_ram_aux_ckbp)>(scalar);
        break;
    case 704:
        ref->sigFromSrams_bore_22_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_22_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_22_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_22_ram_mcp_hold)>(scalar);
        break;
    case 705:
        ref->sigFromSrams_bore_22_cgen = as_scalar<decltype(ref->sigFromSrams_bore_22_cgen)>(scalar);
        wolf->sigFromSrams_bore_22_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_22_cgen)>(scalar);
        break;
    case 706:
        ref->sigFromSrams_bore_23_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_23_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_23_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_23_ram_hold)>(scalar);
        break;
    case 707:
        ref->sigFromSrams_bore_23_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_23_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_23_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_23_ram_bypass)>(scalar);
        break;
    case 708:
        ref->sigFromSrams_bore_23_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_23_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_23_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_23_ram_bp_clken)>(scalar);
        break;
    case 709:
        ref->sigFromSrams_bore_23_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_23_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_23_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_23_ram_aux_clk)>(scalar);
        break;
    case 710:
        ref->sigFromSrams_bore_23_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_23_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_23_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_23_ram_aux_ckbp)>(scalar);
        break;
    case 711:
        ref->sigFromSrams_bore_23_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_23_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_23_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_23_ram_mcp_hold)>(scalar);
        break;
    case 712:
        ref->sigFromSrams_bore_23_cgen = as_scalar<decltype(ref->sigFromSrams_bore_23_cgen)>(scalar);
        wolf->sigFromSrams_bore_23_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_23_cgen)>(scalar);
        break;
    case 713:
        ref->sigFromSrams_bore_24_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_24_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_24_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_24_ram_hold)>(scalar);
        break;
    case 714:
        ref->sigFromSrams_bore_24_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_24_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_24_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_24_ram_bypass)>(scalar);
        break;
    case 715:
        ref->sigFromSrams_bore_24_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_24_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_24_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_24_ram_bp_clken)>(scalar);
        break;
    case 716:
        ref->sigFromSrams_bore_24_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_24_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_24_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_24_ram_aux_clk)>(scalar);
        break;
    case 717:
        ref->sigFromSrams_bore_24_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_24_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_24_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_24_ram_aux_ckbp)>(scalar);
        break;
    case 718:
        ref->sigFromSrams_bore_24_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_24_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_24_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_24_ram_mcp_hold)>(scalar);
        break;
    case 719:
        ref->sigFromSrams_bore_24_cgen = as_scalar<decltype(ref->sigFromSrams_bore_24_cgen)>(scalar);
        wolf->sigFromSrams_bore_24_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_24_cgen)>(scalar);
        break;
    case 720:
        ref->sigFromSrams_bore_25_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_25_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_25_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_25_ram_hold)>(scalar);
        break;
    case 721:
        ref->sigFromSrams_bore_25_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_25_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_25_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_25_ram_bypass)>(scalar);
        break;
    case 722:
        ref->sigFromSrams_bore_25_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_25_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_25_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_25_ram_bp_clken)>(scalar);
        break;
    case 723:
        ref->sigFromSrams_bore_25_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_25_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_25_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_25_ram_aux_clk)>(scalar);
        break;
    case 724:
        ref->sigFromSrams_bore_25_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_25_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_25_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_25_ram_aux_ckbp)>(scalar);
        break;
    case 725:
        ref->sigFromSrams_bore_25_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_25_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_25_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_25_ram_mcp_hold)>(scalar);
        break;
    case 726:
        ref->sigFromSrams_bore_25_cgen = as_scalar<decltype(ref->sigFromSrams_bore_25_cgen)>(scalar);
        wolf->sigFromSrams_bore_25_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_25_cgen)>(scalar);
        break;
    case 727:
        ref->sigFromSrams_bore_26_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_26_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_26_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_26_ram_hold)>(scalar);
        break;
    case 728:
        ref->sigFromSrams_bore_26_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_26_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_26_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_26_ram_bypass)>(scalar);
        break;
    case 729:
        ref->sigFromSrams_bore_26_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_26_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_26_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_26_ram_bp_clken)>(scalar);
        break;
    case 730:
        ref->sigFromSrams_bore_26_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_26_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_26_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_26_ram_aux_clk)>(scalar);
        break;
    case 731:
        ref->sigFromSrams_bore_26_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_26_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_26_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_26_ram_aux_ckbp)>(scalar);
        break;
    case 732:
        ref->sigFromSrams_bore_26_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_26_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_26_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_26_ram_mcp_hold)>(scalar);
        break;
    case 733:
        ref->sigFromSrams_bore_26_cgen = as_scalar<decltype(ref->sigFromSrams_bore_26_cgen)>(scalar);
        wolf->sigFromSrams_bore_26_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_26_cgen)>(scalar);
        break;
    case 734:
        ref->sigFromSrams_bore_27_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_27_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_27_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_27_ram_hold)>(scalar);
        break;
    case 735:
        ref->sigFromSrams_bore_27_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_27_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_27_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_27_ram_bypass)>(scalar);
        break;
    case 736:
        ref->sigFromSrams_bore_27_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_27_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_27_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_27_ram_bp_clken)>(scalar);
        break;
    case 737:
        ref->sigFromSrams_bore_27_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_27_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_27_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_27_ram_aux_clk)>(scalar);
        break;
    case 738:
        ref->sigFromSrams_bore_27_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_27_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_27_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_27_ram_aux_ckbp)>(scalar);
        break;
    case 739:
        ref->sigFromSrams_bore_27_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_27_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_27_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_27_ram_mcp_hold)>(scalar);
        break;
    case 740:
        ref->sigFromSrams_bore_27_cgen = as_scalar<decltype(ref->sigFromSrams_bore_27_cgen)>(scalar);
        wolf->sigFromSrams_bore_27_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_27_cgen)>(scalar);
        break;
    case 741:
        ref->sigFromSrams_bore_28_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_28_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_28_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_28_ram_hold)>(scalar);
        break;
    case 742:
        ref->sigFromSrams_bore_28_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_28_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_28_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_28_ram_bypass)>(scalar);
        break;
    case 743:
        ref->sigFromSrams_bore_28_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_28_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_28_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_28_ram_bp_clken)>(scalar);
        break;
    case 744:
        ref->sigFromSrams_bore_28_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_28_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_28_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_28_ram_aux_clk)>(scalar);
        break;
    case 745:
        ref->sigFromSrams_bore_28_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_28_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_28_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_28_ram_aux_ckbp)>(scalar);
        break;
    case 746:
        ref->sigFromSrams_bore_28_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_28_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_28_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_28_ram_mcp_hold)>(scalar);
        break;
    case 747:
        ref->sigFromSrams_bore_28_cgen = as_scalar<decltype(ref->sigFromSrams_bore_28_cgen)>(scalar);
        wolf->sigFromSrams_bore_28_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_28_cgen)>(scalar);
        break;
    case 748:
        ref->sigFromSrams_bore_29_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_29_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_29_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_29_ram_hold)>(scalar);
        break;
    case 749:
        ref->sigFromSrams_bore_29_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_29_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_29_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_29_ram_bypass)>(scalar);
        break;
    case 750:
        ref->sigFromSrams_bore_29_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_29_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_29_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_29_ram_bp_clken)>(scalar);
        break;
    case 751:
        ref->sigFromSrams_bore_29_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_29_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_29_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_29_ram_aux_clk)>(scalar);
        break;
    case 752:
        ref->sigFromSrams_bore_29_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_29_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_29_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_29_ram_aux_ckbp)>(scalar);
        break;
    case 753:
        ref->sigFromSrams_bore_29_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_29_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_29_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_29_ram_mcp_hold)>(scalar);
        break;
    case 754:
        ref->sigFromSrams_bore_29_cgen = as_scalar<decltype(ref->sigFromSrams_bore_29_cgen)>(scalar);
        wolf->sigFromSrams_bore_29_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_29_cgen)>(scalar);
        break;
    case 755:
        ref->sigFromSrams_bore_30_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_30_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_30_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_30_ram_hold)>(scalar);
        break;
    case 756:
        ref->sigFromSrams_bore_30_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_30_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_30_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_30_ram_bypass)>(scalar);
        break;
    case 757:
        ref->sigFromSrams_bore_30_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_30_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_30_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_30_ram_bp_clken)>(scalar);
        break;
    case 758:
        ref->sigFromSrams_bore_30_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_30_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_30_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_30_ram_aux_clk)>(scalar);
        break;
    case 759:
        ref->sigFromSrams_bore_30_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_30_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_30_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_30_ram_aux_ckbp)>(scalar);
        break;
    case 760:
        ref->sigFromSrams_bore_30_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_30_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_30_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_30_ram_mcp_hold)>(scalar);
        break;
    case 761:
        ref->sigFromSrams_bore_30_cgen = as_scalar<decltype(ref->sigFromSrams_bore_30_cgen)>(scalar);
        wolf->sigFromSrams_bore_30_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_30_cgen)>(scalar);
        break;
    case 762:
        ref->sigFromSrams_bore_31_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_31_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_31_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_31_ram_hold)>(scalar);
        break;
    case 763:
        ref->sigFromSrams_bore_31_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_31_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_31_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_31_ram_bypass)>(scalar);
        break;
    case 764:
        ref->sigFromSrams_bore_31_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_31_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_31_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_31_ram_bp_clken)>(scalar);
        break;
    case 765:
        ref->sigFromSrams_bore_31_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_31_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_31_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_31_ram_aux_clk)>(scalar);
        break;
    case 766:
        ref->sigFromSrams_bore_31_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_31_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_31_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_31_ram_aux_ckbp)>(scalar);
        break;
    case 767:
        ref->sigFromSrams_bore_31_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_31_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_31_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_31_ram_mcp_hold)>(scalar);
        break;
    case 768:
        ref->sigFromSrams_bore_31_cgen = as_scalar<decltype(ref->sigFromSrams_bore_31_cgen)>(scalar);
        wolf->sigFromSrams_bore_31_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_31_cgen)>(scalar);
        break;
    case 769:
        ref->sigFromSrams_bore_32_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_32_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_32_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_32_ram_hold)>(scalar);
        break;
    case 770:
        ref->sigFromSrams_bore_32_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_32_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_32_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_32_ram_bypass)>(scalar);
        break;
    case 771:
        ref->sigFromSrams_bore_32_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_32_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_32_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_32_ram_bp_clken)>(scalar);
        break;
    case 772:
        ref->sigFromSrams_bore_32_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_32_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_32_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_32_ram_aux_clk)>(scalar);
        break;
    case 773:
        ref->sigFromSrams_bore_32_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_32_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_32_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_32_ram_aux_ckbp)>(scalar);
        break;
    case 774:
        ref->sigFromSrams_bore_32_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_32_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_32_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_32_ram_mcp_hold)>(scalar);
        break;
    case 775:
        ref->sigFromSrams_bore_32_cgen = as_scalar<decltype(ref->sigFromSrams_bore_32_cgen)>(scalar);
        wolf->sigFromSrams_bore_32_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_32_cgen)>(scalar);
        break;
    case 776:
        ref->sigFromSrams_bore_33_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_33_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_33_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_33_ram_hold)>(scalar);
        break;
    case 777:
        ref->sigFromSrams_bore_33_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_33_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_33_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_33_ram_bypass)>(scalar);
        break;
    case 778:
        ref->sigFromSrams_bore_33_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_33_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_33_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_33_ram_bp_clken)>(scalar);
        break;
    case 779:
        ref->sigFromSrams_bore_33_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_33_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_33_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_33_ram_aux_clk)>(scalar);
        break;
    case 780:
        ref->sigFromSrams_bore_33_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_33_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_33_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_33_ram_aux_ckbp)>(scalar);
        break;
    case 781:
        ref->sigFromSrams_bore_33_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_33_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_33_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_33_ram_mcp_hold)>(scalar);
        break;
    case 782:
        ref->sigFromSrams_bore_33_cgen = as_scalar<decltype(ref->sigFromSrams_bore_33_cgen)>(scalar);
        wolf->sigFromSrams_bore_33_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_33_cgen)>(scalar);
        break;
    case 783:
        ref->sigFromSrams_bore_34_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_34_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_34_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_34_ram_hold)>(scalar);
        break;
    case 784:
        ref->sigFromSrams_bore_34_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_34_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_34_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_34_ram_bypass)>(scalar);
        break;
    case 785:
        ref->sigFromSrams_bore_34_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_34_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_34_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_34_ram_bp_clken)>(scalar);
        break;
    case 786:
        ref->sigFromSrams_bore_34_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_34_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_34_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_34_ram_aux_clk)>(scalar);
        break;
    case 787:
        ref->sigFromSrams_bore_34_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_34_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_34_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_34_ram_aux_ckbp)>(scalar);
        break;
    case 788:
        ref->sigFromSrams_bore_34_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_34_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_34_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_34_ram_mcp_hold)>(scalar);
        break;
    case 789:
        ref->sigFromSrams_bore_34_cgen = as_scalar<decltype(ref->sigFromSrams_bore_34_cgen)>(scalar);
        wolf->sigFromSrams_bore_34_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_34_cgen)>(scalar);
        break;
    case 790:
        ref->sigFromSrams_bore_35_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_35_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_35_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_35_ram_hold)>(scalar);
        break;
    case 791:
        ref->sigFromSrams_bore_35_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_35_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_35_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_35_ram_bypass)>(scalar);
        break;
    case 792:
        ref->sigFromSrams_bore_35_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_35_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_35_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_35_ram_bp_clken)>(scalar);
        break;
    case 793:
        ref->sigFromSrams_bore_35_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_35_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_35_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_35_ram_aux_clk)>(scalar);
        break;
    case 794:
        ref->sigFromSrams_bore_35_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_35_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_35_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_35_ram_aux_ckbp)>(scalar);
        break;
    case 795:
        ref->sigFromSrams_bore_35_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_35_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_35_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_35_ram_mcp_hold)>(scalar);
        break;
    case 796:
        ref->sigFromSrams_bore_35_cgen = as_scalar<decltype(ref->sigFromSrams_bore_35_cgen)>(scalar);
        wolf->sigFromSrams_bore_35_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_35_cgen)>(scalar);
        break;
    case 797:
        ref->sigFromSrams_bore_36_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_36_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_36_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_36_ram_hold)>(scalar);
        break;
    case 798:
        ref->sigFromSrams_bore_36_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_36_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_36_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_36_ram_bypass)>(scalar);
        break;
    case 799:
        ref->sigFromSrams_bore_36_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_36_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_36_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_36_ram_bp_clken)>(scalar);
        break;
    case 800:
        ref->sigFromSrams_bore_36_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_36_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_36_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_36_ram_aux_clk)>(scalar);
        break;
    case 801:
        ref->sigFromSrams_bore_36_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_36_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_36_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_36_ram_aux_ckbp)>(scalar);
        break;
    case 802:
        ref->sigFromSrams_bore_36_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_36_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_36_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_36_ram_mcp_hold)>(scalar);
        break;
    case 803:
        ref->sigFromSrams_bore_36_cgen = as_scalar<decltype(ref->sigFromSrams_bore_36_cgen)>(scalar);
        wolf->sigFromSrams_bore_36_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_36_cgen)>(scalar);
        break;
    case 804:
        ref->sigFromSrams_bore_37_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_37_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_37_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_37_ram_hold)>(scalar);
        break;
    case 805:
        ref->sigFromSrams_bore_37_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_37_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_37_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_37_ram_bypass)>(scalar);
        break;
    case 806:
        ref->sigFromSrams_bore_37_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_37_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_37_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_37_ram_bp_clken)>(scalar);
        break;
    case 807:
        ref->sigFromSrams_bore_37_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_37_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_37_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_37_ram_aux_clk)>(scalar);
        break;
    case 808:
        ref->sigFromSrams_bore_37_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_37_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_37_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_37_ram_aux_ckbp)>(scalar);
        break;
    case 809:
        ref->sigFromSrams_bore_37_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_37_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_37_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_37_ram_mcp_hold)>(scalar);
        break;
    case 810:
        ref->sigFromSrams_bore_37_cgen = as_scalar<decltype(ref->sigFromSrams_bore_37_cgen)>(scalar);
        wolf->sigFromSrams_bore_37_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_37_cgen)>(scalar);
        break;
    case 811:
        ref->sigFromSrams_bore_38_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_38_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_38_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_38_ram_hold)>(scalar);
        break;
    case 812:
        ref->sigFromSrams_bore_38_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_38_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_38_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_38_ram_bypass)>(scalar);
        break;
    case 813:
        ref->sigFromSrams_bore_38_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_38_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_38_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_38_ram_bp_clken)>(scalar);
        break;
    case 814:
        ref->sigFromSrams_bore_38_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_38_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_38_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_38_ram_aux_clk)>(scalar);
        break;
    case 815:
        ref->sigFromSrams_bore_38_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_38_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_38_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_38_ram_aux_ckbp)>(scalar);
        break;
    case 816:
        ref->sigFromSrams_bore_38_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_38_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_38_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_38_ram_mcp_hold)>(scalar);
        break;
    case 817:
        ref->sigFromSrams_bore_38_cgen = as_scalar<decltype(ref->sigFromSrams_bore_38_cgen)>(scalar);
        wolf->sigFromSrams_bore_38_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_38_cgen)>(scalar);
        break;
    case 818:
        ref->sigFromSrams_bore_39_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_39_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_39_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_39_ram_hold)>(scalar);
        break;
    case 819:
        ref->sigFromSrams_bore_39_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_39_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_39_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_39_ram_bypass)>(scalar);
        break;
    case 820:
        ref->sigFromSrams_bore_39_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_39_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_39_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_39_ram_bp_clken)>(scalar);
        break;
    case 821:
        ref->sigFromSrams_bore_39_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_39_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_39_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_39_ram_aux_clk)>(scalar);
        break;
    case 822:
        ref->sigFromSrams_bore_39_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_39_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_39_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_39_ram_aux_ckbp)>(scalar);
        break;
    case 823:
        ref->sigFromSrams_bore_39_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_39_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_39_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_39_ram_mcp_hold)>(scalar);
        break;
    case 824:
        ref->sigFromSrams_bore_39_cgen = as_scalar<decltype(ref->sigFromSrams_bore_39_cgen)>(scalar);
        wolf->sigFromSrams_bore_39_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_39_cgen)>(scalar);
        break;
    case 825:
        ref->sigFromSrams_bore_40_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_40_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_40_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_40_ram_hold)>(scalar);
        break;
    case 826:
        ref->sigFromSrams_bore_40_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_40_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_40_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_40_ram_bypass)>(scalar);
        break;
    case 827:
        ref->sigFromSrams_bore_40_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_40_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_40_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_40_ram_bp_clken)>(scalar);
        break;
    case 828:
        ref->sigFromSrams_bore_40_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_40_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_40_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_40_ram_aux_clk)>(scalar);
        break;
    case 829:
        ref->sigFromSrams_bore_40_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_40_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_40_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_40_ram_aux_ckbp)>(scalar);
        break;
    case 830:
        ref->sigFromSrams_bore_40_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_40_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_40_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_40_ram_mcp_hold)>(scalar);
        break;
    case 831:
        ref->sigFromSrams_bore_40_cgen = as_scalar<decltype(ref->sigFromSrams_bore_40_cgen)>(scalar);
        wolf->sigFromSrams_bore_40_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_40_cgen)>(scalar);
        break;
    case 832:
        ref->sigFromSrams_bore_41_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_41_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_41_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_41_ram_hold)>(scalar);
        break;
    case 833:
        ref->sigFromSrams_bore_41_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_41_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_41_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_41_ram_bypass)>(scalar);
        break;
    case 834:
        ref->sigFromSrams_bore_41_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_41_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_41_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_41_ram_bp_clken)>(scalar);
        break;
    case 835:
        ref->sigFromSrams_bore_41_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_41_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_41_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_41_ram_aux_clk)>(scalar);
        break;
    case 836:
        ref->sigFromSrams_bore_41_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_41_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_41_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_41_ram_aux_ckbp)>(scalar);
        break;
    case 837:
        ref->sigFromSrams_bore_41_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_41_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_41_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_41_ram_mcp_hold)>(scalar);
        break;
    case 838:
        ref->sigFromSrams_bore_41_cgen = as_scalar<decltype(ref->sigFromSrams_bore_41_cgen)>(scalar);
        wolf->sigFromSrams_bore_41_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_41_cgen)>(scalar);
        break;
    case 839:
        ref->sigFromSrams_bore_42_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_42_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_42_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_42_ram_hold)>(scalar);
        break;
    case 840:
        ref->sigFromSrams_bore_42_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_42_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_42_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_42_ram_bypass)>(scalar);
        break;
    case 841:
        ref->sigFromSrams_bore_42_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_42_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_42_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_42_ram_bp_clken)>(scalar);
        break;
    case 842:
        ref->sigFromSrams_bore_42_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_42_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_42_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_42_ram_aux_clk)>(scalar);
        break;
    case 843:
        ref->sigFromSrams_bore_42_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_42_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_42_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_42_ram_aux_ckbp)>(scalar);
        break;
    case 844:
        ref->sigFromSrams_bore_42_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_42_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_42_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_42_ram_mcp_hold)>(scalar);
        break;
    case 845:
        ref->sigFromSrams_bore_42_cgen = as_scalar<decltype(ref->sigFromSrams_bore_42_cgen)>(scalar);
        wolf->sigFromSrams_bore_42_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_42_cgen)>(scalar);
        break;
    case 846:
        ref->sigFromSrams_bore_43_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_43_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_43_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_43_ram_hold)>(scalar);
        break;
    case 847:
        ref->sigFromSrams_bore_43_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_43_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_43_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_43_ram_bypass)>(scalar);
        break;
    case 848:
        ref->sigFromSrams_bore_43_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_43_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_43_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_43_ram_bp_clken)>(scalar);
        break;
    case 849:
        ref->sigFromSrams_bore_43_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_43_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_43_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_43_ram_aux_clk)>(scalar);
        break;
    case 850:
        ref->sigFromSrams_bore_43_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_43_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_43_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_43_ram_aux_ckbp)>(scalar);
        break;
    case 851:
        ref->sigFromSrams_bore_43_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_43_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_43_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_43_ram_mcp_hold)>(scalar);
        break;
    case 852:
        ref->sigFromSrams_bore_43_cgen = as_scalar<decltype(ref->sigFromSrams_bore_43_cgen)>(scalar);
        wolf->sigFromSrams_bore_43_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_43_cgen)>(scalar);
        break;
    case 853:
        ref->sigFromSrams_bore_44_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_44_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_44_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_44_ram_hold)>(scalar);
        break;
    case 854:
        ref->sigFromSrams_bore_44_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_44_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_44_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_44_ram_bypass)>(scalar);
        break;
    case 855:
        ref->sigFromSrams_bore_44_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_44_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_44_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_44_ram_bp_clken)>(scalar);
        break;
    case 856:
        ref->sigFromSrams_bore_44_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_44_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_44_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_44_ram_aux_clk)>(scalar);
        break;
    case 857:
        ref->sigFromSrams_bore_44_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_44_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_44_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_44_ram_aux_ckbp)>(scalar);
        break;
    case 858:
        ref->sigFromSrams_bore_44_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_44_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_44_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_44_ram_mcp_hold)>(scalar);
        break;
    case 859:
        ref->sigFromSrams_bore_44_cgen = as_scalar<decltype(ref->sigFromSrams_bore_44_cgen)>(scalar);
        wolf->sigFromSrams_bore_44_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_44_cgen)>(scalar);
        break;
    case 860:
        ref->sigFromSrams_bore_45_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_45_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_45_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_45_ram_hold)>(scalar);
        break;
    case 861:
        ref->sigFromSrams_bore_45_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_45_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_45_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_45_ram_bypass)>(scalar);
        break;
    case 862:
        ref->sigFromSrams_bore_45_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_45_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_45_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_45_ram_bp_clken)>(scalar);
        break;
    case 863:
        ref->sigFromSrams_bore_45_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_45_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_45_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_45_ram_aux_clk)>(scalar);
        break;
    case 864:
        ref->sigFromSrams_bore_45_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_45_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_45_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_45_ram_aux_ckbp)>(scalar);
        break;
    case 865:
        ref->sigFromSrams_bore_45_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_45_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_45_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_45_ram_mcp_hold)>(scalar);
        break;
    case 866:
        ref->sigFromSrams_bore_45_cgen = as_scalar<decltype(ref->sigFromSrams_bore_45_cgen)>(scalar);
        wolf->sigFromSrams_bore_45_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_45_cgen)>(scalar);
        break;
    case 867:
        ref->sigFromSrams_bore_46_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_46_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_46_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_46_ram_hold)>(scalar);
        break;
    case 868:
        ref->sigFromSrams_bore_46_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_46_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_46_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_46_ram_bypass)>(scalar);
        break;
    case 869:
        ref->sigFromSrams_bore_46_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_46_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_46_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_46_ram_bp_clken)>(scalar);
        break;
    case 870:
        ref->sigFromSrams_bore_46_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_46_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_46_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_46_ram_aux_clk)>(scalar);
        break;
    case 871:
        ref->sigFromSrams_bore_46_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_46_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_46_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_46_ram_aux_ckbp)>(scalar);
        break;
    case 872:
        ref->sigFromSrams_bore_46_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_46_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_46_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_46_ram_mcp_hold)>(scalar);
        break;
    case 873:
        ref->sigFromSrams_bore_46_cgen = as_scalar<decltype(ref->sigFromSrams_bore_46_cgen)>(scalar);
        wolf->sigFromSrams_bore_46_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_46_cgen)>(scalar);
        break;
    case 874:
        ref->sigFromSrams_bore_47_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_47_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_47_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_47_ram_hold)>(scalar);
        break;
    case 875:
        ref->sigFromSrams_bore_47_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_47_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_47_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_47_ram_bypass)>(scalar);
        break;
    case 876:
        ref->sigFromSrams_bore_47_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_47_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_47_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_47_ram_bp_clken)>(scalar);
        break;
    case 877:
        ref->sigFromSrams_bore_47_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_47_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_47_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_47_ram_aux_clk)>(scalar);
        break;
    case 878:
        ref->sigFromSrams_bore_47_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_47_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_47_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_47_ram_aux_ckbp)>(scalar);
        break;
    case 879:
        ref->sigFromSrams_bore_47_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_47_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_47_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_47_ram_mcp_hold)>(scalar);
        break;
    case 880:
        ref->sigFromSrams_bore_47_cgen = as_scalar<decltype(ref->sigFromSrams_bore_47_cgen)>(scalar);
        wolf->sigFromSrams_bore_47_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_47_cgen)>(scalar);
        break;
    case 881:
        ref->sigFromSrams_bore_48_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_48_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_48_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_48_ram_hold)>(scalar);
        break;
    case 882:
        ref->sigFromSrams_bore_48_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_48_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_48_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_48_ram_bypass)>(scalar);
        break;
    case 883:
        ref->sigFromSrams_bore_48_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_48_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_48_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_48_ram_bp_clken)>(scalar);
        break;
    case 884:
        ref->sigFromSrams_bore_48_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_48_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_48_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_48_ram_aux_clk)>(scalar);
        break;
    case 885:
        ref->sigFromSrams_bore_48_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_48_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_48_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_48_ram_aux_ckbp)>(scalar);
        break;
    case 886:
        ref->sigFromSrams_bore_48_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_48_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_48_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_48_ram_mcp_hold)>(scalar);
        break;
    case 887:
        ref->sigFromSrams_bore_48_cgen = as_scalar<decltype(ref->sigFromSrams_bore_48_cgen)>(scalar);
        wolf->sigFromSrams_bore_48_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_48_cgen)>(scalar);
        break;
    case 888:
        ref->sigFromSrams_bore_49_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_49_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_49_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_49_ram_hold)>(scalar);
        break;
    case 889:
        ref->sigFromSrams_bore_49_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_49_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_49_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_49_ram_bypass)>(scalar);
        break;
    case 890:
        ref->sigFromSrams_bore_49_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_49_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_49_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_49_ram_bp_clken)>(scalar);
        break;
    case 891:
        ref->sigFromSrams_bore_49_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_49_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_49_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_49_ram_aux_clk)>(scalar);
        break;
    case 892:
        ref->sigFromSrams_bore_49_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_49_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_49_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_49_ram_aux_ckbp)>(scalar);
        break;
    case 893:
        ref->sigFromSrams_bore_49_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_49_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_49_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_49_ram_mcp_hold)>(scalar);
        break;
    case 894:
        ref->sigFromSrams_bore_49_cgen = as_scalar<decltype(ref->sigFromSrams_bore_49_cgen)>(scalar);
        wolf->sigFromSrams_bore_49_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_49_cgen)>(scalar);
        break;
    case 895:
        ref->sigFromSrams_bore_50_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_50_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_50_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_50_ram_hold)>(scalar);
        break;
    case 896:
        ref->sigFromSrams_bore_50_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_50_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_50_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_50_ram_bypass)>(scalar);
        break;
    case 897:
        ref->sigFromSrams_bore_50_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_50_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_50_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_50_ram_bp_clken)>(scalar);
        break;
    case 898:
        ref->sigFromSrams_bore_50_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_50_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_50_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_50_ram_aux_clk)>(scalar);
        break;
    case 899:
        ref->sigFromSrams_bore_50_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_50_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_50_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_50_ram_aux_ckbp)>(scalar);
        break;
    case 900:
        ref->sigFromSrams_bore_50_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_50_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_50_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_50_ram_mcp_hold)>(scalar);
        break;
    case 901:
        ref->sigFromSrams_bore_50_cgen = as_scalar<decltype(ref->sigFromSrams_bore_50_cgen)>(scalar);
        wolf->sigFromSrams_bore_50_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_50_cgen)>(scalar);
        break;
    case 902:
        ref->sigFromSrams_bore_51_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_51_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_51_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_51_ram_hold)>(scalar);
        break;
    case 903:
        ref->sigFromSrams_bore_51_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_51_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_51_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_51_ram_bypass)>(scalar);
        break;
    case 904:
        ref->sigFromSrams_bore_51_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_51_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_51_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_51_ram_bp_clken)>(scalar);
        break;
    case 905:
        ref->sigFromSrams_bore_51_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_51_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_51_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_51_ram_aux_clk)>(scalar);
        break;
    case 906:
        ref->sigFromSrams_bore_51_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_51_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_51_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_51_ram_aux_ckbp)>(scalar);
        break;
    case 907:
        ref->sigFromSrams_bore_51_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_51_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_51_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_51_ram_mcp_hold)>(scalar);
        break;
    case 908:
        ref->sigFromSrams_bore_51_cgen = as_scalar<decltype(ref->sigFromSrams_bore_51_cgen)>(scalar);
        wolf->sigFromSrams_bore_51_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_51_cgen)>(scalar);
        break;
    case 909:
        ref->sigFromSrams_bore_52_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_52_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_52_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_52_ram_hold)>(scalar);
        break;
    case 910:
        ref->sigFromSrams_bore_52_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_52_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_52_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_52_ram_bypass)>(scalar);
        break;
    case 911:
        ref->sigFromSrams_bore_52_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_52_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_52_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_52_ram_bp_clken)>(scalar);
        break;
    case 912:
        ref->sigFromSrams_bore_52_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_52_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_52_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_52_ram_aux_clk)>(scalar);
        break;
    case 913:
        ref->sigFromSrams_bore_52_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_52_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_52_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_52_ram_aux_ckbp)>(scalar);
        break;
    case 914:
        ref->sigFromSrams_bore_52_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_52_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_52_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_52_ram_mcp_hold)>(scalar);
        break;
    case 915:
        ref->sigFromSrams_bore_52_cgen = as_scalar<decltype(ref->sigFromSrams_bore_52_cgen)>(scalar);
        wolf->sigFromSrams_bore_52_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_52_cgen)>(scalar);
        break;
    case 916:
        ref->sigFromSrams_bore_53_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_53_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_53_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_53_ram_hold)>(scalar);
        break;
    case 917:
        ref->sigFromSrams_bore_53_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_53_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_53_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_53_ram_bypass)>(scalar);
        break;
    case 918:
        ref->sigFromSrams_bore_53_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_53_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_53_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_53_ram_bp_clken)>(scalar);
        break;
    case 919:
        ref->sigFromSrams_bore_53_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_53_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_53_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_53_ram_aux_clk)>(scalar);
        break;
    case 920:
        ref->sigFromSrams_bore_53_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_53_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_53_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_53_ram_aux_ckbp)>(scalar);
        break;
    case 921:
        ref->sigFromSrams_bore_53_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_53_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_53_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_53_ram_mcp_hold)>(scalar);
        break;
    case 922:
        ref->sigFromSrams_bore_53_cgen = as_scalar<decltype(ref->sigFromSrams_bore_53_cgen)>(scalar);
        wolf->sigFromSrams_bore_53_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_53_cgen)>(scalar);
        break;
    case 923:
        ref->sigFromSrams_bore_54_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_54_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_54_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_54_ram_hold)>(scalar);
        break;
    case 924:
        ref->sigFromSrams_bore_54_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_54_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_54_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_54_ram_bypass)>(scalar);
        break;
    case 925:
        ref->sigFromSrams_bore_54_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_54_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_54_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_54_ram_bp_clken)>(scalar);
        break;
    case 926:
        ref->sigFromSrams_bore_54_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_54_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_54_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_54_ram_aux_clk)>(scalar);
        break;
    case 927:
        ref->sigFromSrams_bore_54_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_54_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_54_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_54_ram_aux_ckbp)>(scalar);
        break;
    case 928:
        ref->sigFromSrams_bore_54_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_54_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_54_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_54_ram_mcp_hold)>(scalar);
        break;
    case 929:
        ref->sigFromSrams_bore_54_cgen = as_scalar<decltype(ref->sigFromSrams_bore_54_cgen)>(scalar);
        wolf->sigFromSrams_bore_54_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_54_cgen)>(scalar);
        break;
    case 930:
        ref->sigFromSrams_bore_55_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_55_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_55_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_55_ram_hold)>(scalar);
        break;
    case 931:
        ref->sigFromSrams_bore_55_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_55_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_55_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_55_ram_bypass)>(scalar);
        break;
    case 932:
        ref->sigFromSrams_bore_55_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_55_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_55_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_55_ram_bp_clken)>(scalar);
        break;
    case 933:
        ref->sigFromSrams_bore_55_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_55_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_55_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_55_ram_aux_clk)>(scalar);
        break;
    case 934:
        ref->sigFromSrams_bore_55_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_55_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_55_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_55_ram_aux_ckbp)>(scalar);
        break;
    case 935:
        ref->sigFromSrams_bore_55_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_55_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_55_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_55_ram_mcp_hold)>(scalar);
        break;
    case 936:
        ref->sigFromSrams_bore_55_cgen = as_scalar<decltype(ref->sigFromSrams_bore_55_cgen)>(scalar);
        wolf->sigFromSrams_bore_55_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_55_cgen)>(scalar);
        break;
    case 937:
        ref->sigFromSrams_bore_56_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_56_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_56_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_56_ram_hold)>(scalar);
        break;
    case 938:
        ref->sigFromSrams_bore_56_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_56_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_56_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_56_ram_bypass)>(scalar);
        break;
    case 939:
        ref->sigFromSrams_bore_56_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_56_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_56_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_56_ram_bp_clken)>(scalar);
        break;
    case 940:
        ref->sigFromSrams_bore_56_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_56_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_56_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_56_ram_aux_clk)>(scalar);
        break;
    case 941:
        ref->sigFromSrams_bore_56_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_56_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_56_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_56_ram_aux_ckbp)>(scalar);
        break;
    case 942:
        ref->sigFromSrams_bore_56_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_56_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_56_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_56_ram_mcp_hold)>(scalar);
        break;
    case 943:
        ref->sigFromSrams_bore_56_cgen = as_scalar<decltype(ref->sigFromSrams_bore_56_cgen)>(scalar);
        wolf->sigFromSrams_bore_56_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_56_cgen)>(scalar);
        break;
    case 944:
        ref->sigFromSrams_bore_57_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_57_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_57_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_57_ram_hold)>(scalar);
        break;
    case 945:
        ref->sigFromSrams_bore_57_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_57_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_57_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_57_ram_bypass)>(scalar);
        break;
    case 946:
        ref->sigFromSrams_bore_57_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_57_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_57_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_57_ram_bp_clken)>(scalar);
        break;
    case 947:
        ref->sigFromSrams_bore_57_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_57_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_57_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_57_ram_aux_clk)>(scalar);
        break;
    case 948:
        ref->sigFromSrams_bore_57_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_57_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_57_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_57_ram_aux_ckbp)>(scalar);
        break;
    case 949:
        ref->sigFromSrams_bore_57_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_57_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_57_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_57_ram_mcp_hold)>(scalar);
        break;
    case 950:
        ref->sigFromSrams_bore_57_cgen = as_scalar<decltype(ref->sigFromSrams_bore_57_cgen)>(scalar);
        wolf->sigFromSrams_bore_57_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_57_cgen)>(scalar);
        break;
    case 951:
        ref->sigFromSrams_bore_58_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_58_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_58_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_58_ram_hold)>(scalar);
        break;
    case 952:
        ref->sigFromSrams_bore_58_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_58_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_58_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_58_ram_bypass)>(scalar);
        break;
    case 953:
        ref->sigFromSrams_bore_58_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_58_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_58_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_58_ram_bp_clken)>(scalar);
        break;
    case 954:
        ref->sigFromSrams_bore_58_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_58_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_58_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_58_ram_aux_clk)>(scalar);
        break;
    case 955:
        ref->sigFromSrams_bore_58_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_58_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_58_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_58_ram_aux_ckbp)>(scalar);
        break;
    case 956:
        ref->sigFromSrams_bore_58_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_58_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_58_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_58_ram_mcp_hold)>(scalar);
        break;
    case 957:
        ref->sigFromSrams_bore_58_cgen = as_scalar<decltype(ref->sigFromSrams_bore_58_cgen)>(scalar);
        wolf->sigFromSrams_bore_58_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_58_cgen)>(scalar);
        break;
    case 958:
        ref->sigFromSrams_bore_59_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_59_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_59_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_59_ram_hold)>(scalar);
        break;
    case 959:
        ref->sigFromSrams_bore_59_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_59_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_59_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_59_ram_bypass)>(scalar);
        break;
    case 960:
        ref->sigFromSrams_bore_59_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_59_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_59_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_59_ram_bp_clken)>(scalar);
        break;
    case 961:
        ref->sigFromSrams_bore_59_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_59_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_59_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_59_ram_aux_clk)>(scalar);
        break;
    case 962:
        ref->sigFromSrams_bore_59_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_59_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_59_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_59_ram_aux_ckbp)>(scalar);
        break;
    case 963:
        ref->sigFromSrams_bore_59_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_59_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_59_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_59_ram_mcp_hold)>(scalar);
        break;
    case 964:
        ref->sigFromSrams_bore_59_cgen = as_scalar<decltype(ref->sigFromSrams_bore_59_cgen)>(scalar);
        wolf->sigFromSrams_bore_59_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_59_cgen)>(scalar);
        break;
    case 965:
        ref->sigFromSrams_bore_60_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_60_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_60_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_60_ram_hold)>(scalar);
        break;
    case 966:
        ref->sigFromSrams_bore_60_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_60_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_60_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_60_ram_bypass)>(scalar);
        break;
    case 967:
        ref->sigFromSrams_bore_60_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_60_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_60_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_60_ram_bp_clken)>(scalar);
        break;
    case 968:
        ref->sigFromSrams_bore_60_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_60_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_60_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_60_ram_aux_clk)>(scalar);
        break;
    case 969:
        ref->sigFromSrams_bore_60_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_60_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_60_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_60_ram_aux_ckbp)>(scalar);
        break;
    case 970:
        ref->sigFromSrams_bore_60_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_60_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_60_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_60_ram_mcp_hold)>(scalar);
        break;
    case 971:
        ref->sigFromSrams_bore_60_cgen = as_scalar<decltype(ref->sigFromSrams_bore_60_cgen)>(scalar);
        wolf->sigFromSrams_bore_60_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_60_cgen)>(scalar);
        break;
    case 972:
        ref->sigFromSrams_bore_61_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_61_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_61_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_61_ram_hold)>(scalar);
        break;
    case 973:
        ref->sigFromSrams_bore_61_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_61_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_61_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_61_ram_bypass)>(scalar);
        break;
    case 974:
        ref->sigFromSrams_bore_61_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_61_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_61_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_61_ram_bp_clken)>(scalar);
        break;
    case 975:
        ref->sigFromSrams_bore_61_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_61_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_61_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_61_ram_aux_clk)>(scalar);
        break;
    case 976:
        ref->sigFromSrams_bore_61_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_61_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_61_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_61_ram_aux_ckbp)>(scalar);
        break;
    case 977:
        ref->sigFromSrams_bore_61_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_61_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_61_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_61_ram_mcp_hold)>(scalar);
        break;
    case 978:
        ref->sigFromSrams_bore_61_cgen = as_scalar<decltype(ref->sigFromSrams_bore_61_cgen)>(scalar);
        wolf->sigFromSrams_bore_61_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_61_cgen)>(scalar);
        break;
    case 979:
        ref->sigFromSrams_bore_62_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_62_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_62_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_62_ram_hold)>(scalar);
        break;
    case 980:
        ref->sigFromSrams_bore_62_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_62_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_62_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_62_ram_bypass)>(scalar);
        break;
    case 981:
        ref->sigFromSrams_bore_62_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_62_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_62_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_62_ram_bp_clken)>(scalar);
        break;
    case 982:
        ref->sigFromSrams_bore_62_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_62_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_62_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_62_ram_aux_clk)>(scalar);
        break;
    case 983:
        ref->sigFromSrams_bore_62_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_62_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_62_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_62_ram_aux_ckbp)>(scalar);
        break;
    case 984:
        ref->sigFromSrams_bore_62_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_62_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_62_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_62_ram_mcp_hold)>(scalar);
        break;
    case 985:
        ref->sigFromSrams_bore_62_cgen = as_scalar<decltype(ref->sigFromSrams_bore_62_cgen)>(scalar);
        wolf->sigFromSrams_bore_62_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_62_cgen)>(scalar);
        break;
    case 986:
        ref->sigFromSrams_bore_63_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_63_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_63_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_63_ram_hold)>(scalar);
        break;
    case 987:
        ref->sigFromSrams_bore_63_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_63_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_63_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_63_ram_bypass)>(scalar);
        break;
    case 988:
        ref->sigFromSrams_bore_63_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_63_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_63_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_63_ram_bp_clken)>(scalar);
        break;
    case 989:
        ref->sigFromSrams_bore_63_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_63_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_63_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_63_ram_aux_clk)>(scalar);
        break;
    case 990:
        ref->sigFromSrams_bore_63_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_63_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_63_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_63_ram_aux_ckbp)>(scalar);
        break;
    case 991:
        ref->sigFromSrams_bore_63_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_63_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_63_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_63_ram_mcp_hold)>(scalar);
        break;
    case 992:
        ref->sigFromSrams_bore_63_cgen = as_scalar<decltype(ref->sigFromSrams_bore_63_cgen)>(scalar);
        wolf->sigFromSrams_bore_63_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_63_cgen)>(scalar);
        break;
    case 993:
        ref->sigFromSrams_bore_64_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_64_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_64_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_64_ram_hold)>(scalar);
        break;
    case 994:
        ref->sigFromSrams_bore_64_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_64_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_64_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_64_ram_bypass)>(scalar);
        break;
    case 995:
        ref->sigFromSrams_bore_64_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_64_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_64_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_64_ram_bp_clken)>(scalar);
        break;
    case 996:
        ref->sigFromSrams_bore_64_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_64_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_64_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_64_ram_aux_clk)>(scalar);
        break;
    case 997:
        ref->sigFromSrams_bore_64_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_64_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_64_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_64_ram_aux_ckbp)>(scalar);
        break;
    case 998:
        ref->sigFromSrams_bore_64_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_64_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_64_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_64_ram_mcp_hold)>(scalar);
        break;
    case 999:
        ref->sigFromSrams_bore_64_cgen = as_scalar<decltype(ref->sigFromSrams_bore_64_cgen)>(scalar);
        wolf->sigFromSrams_bore_64_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_64_cgen)>(scalar);
        break;
    case 1000:
        ref->sigFromSrams_bore_65_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_65_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_65_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_65_ram_hold)>(scalar);
        break;
    case 1001:
        ref->sigFromSrams_bore_65_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_65_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_65_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_65_ram_bypass)>(scalar);
        break;
    case 1002:
        ref->sigFromSrams_bore_65_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_65_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_65_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_65_ram_bp_clken)>(scalar);
        break;
    case 1003:
        ref->sigFromSrams_bore_65_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_65_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_65_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_65_ram_aux_clk)>(scalar);
        break;
    case 1004:
        ref->sigFromSrams_bore_65_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_65_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_65_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_65_ram_aux_ckbp)>(scalar);
        break;
    case 1005:
        ref->sigFromSrams_bore_65_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_65_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_65_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_65_ram_mcp_hold)>(scalar);
        break;
    case 1006:
        ref->sigFromSrams_bore_65_cgen = as_scalar<decltype(ref->sigFromSrams_bore_65_cgen)>(scalar);
        wolf->sigFromSrams_bore_65_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_65_cgen)>(scalar);
        break;
    case 1007:
        ref->sigFromSrams_bore_66_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_66_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_66_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_66_ram_hold)>(scalar);
        break;
    case 1008:
        ref->sigFromSrams_bore_66_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_66_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_66_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_66_ram_bypass)>(scalar);
        break;
    case 1009:
        ref->sigFromSrams_bore_66_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_66_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_66_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_66_ram_bp_clken)>(scalar);
        break;
    case 1010:
        ref->sigFromSrams_bore_66_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_66_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_66_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_66_ram_aux_clk)>(scalar);
        break;
    case 1011:
        ref->sigFromSrams_bore_66_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_66_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_66_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_66_ram_aux_ckbp)>(scalar);
        break;
    case 1012:
        ref->sigFromSrams_bore_66_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_66_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_66_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_66_ram_mcp_hold)>(scalar);
        break;
    case 1013:
        ref->sigFromSrams_bore_66_cgen = as_scalar<decltype(ref->sigFromSrams_bore_66_cgen)>(scalar);
        wolf->sigFromSrams_bore_66_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_66_cgen)>(scalar);
        break;
    case 1014:
        ref->sigFromSrams_bore_67_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_67_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_67_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_67_ram_hold)>(scalar);
        break;
    case 1015:
        ref->sigFromSrams_bore_67_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_67_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_67_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_67_ram_bypass)>(scalar);
        break;
    case 1016:
        ref->sigFromSrams_bore_67_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_67_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_67_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_67_ram_bp_clken)>(scalar);
        break;
    case 1017:
        ref->sigFromSrams_bore_67_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_67_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_67_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_67_ram_aux_clk)>(scalar);
        break;
    case 1018:
        ref->sigFromSrams_bore_67_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_67_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_67_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_67_ram_aux_ckbp)>(scalar);
        break;
    case 1019:
        ref->sigFromSrams_bore_67_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_67_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_67_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_67_ram_mcp_hold)>(scalar);
        break;
    case 1020:
        ref->sigFromSrams_bore_67_cgen = as_scalar<decltype(ref->sigFromSrams_bore_67_cgen)>(scalar);
        wolf->sigFromSrams_bore_67_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_67_cgen)>(scalar);
        break;
    case 1021:
        ref->sigFromSrams_bore_68_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_68_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_68_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_68_ram_hold)>(scalar);
        break;
    case 1022:
        ref->sigFromSrams_bore_68_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_68_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_68_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_68_ram_bypass)>(scalar);
        break;
    case 1023:
        ref->sigFromSrams_bore_68_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_68_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_68_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_68_ram_bp_clken)>(scalar);
        break;
    case 1024:
        ref->sigFromSrams_bore_68_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_68_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_68_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_68_ram_aux_clk)>(scalar);
        break;
    case 1025:
        ref->sigFromSrams_bore_68_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_68_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_68_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_68_ram_aux_ckbp)>(scalar);
        break;
    case 1026:
        ref->sigFromSrams_bore_68_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_68_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_68_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_68_ram_mcp_hold)>(scalar);
        break;
    case 1027:
        ref->sigFromSrams_bore_68_cgen = as_scalar<decltype(ref->sigFromSrams_bore_68_cgen)>(scalar);
        wolf->sigFromSrams_bore_68_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_68_cgen)>(scalar);
        break;
    case 1028:
        ref->sigFromSrams_bore_69_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_69_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_69_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_69_ram_hold)>(scalar);
        break;
    case 1029:
        ref->sigFromSrams_bore_69_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_69_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_69_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_69_ram_bypass)>(scalar);
        break;
    case 1030:
        ref->sigFromSrams_bore_69_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_69_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_69_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_69_ram_bp_clken)>(scalar);
        break;
    case 1031:
        ref->sigFromSrams_bore_69_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_69_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_69_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_69_ram_aux_clk)>(scalar);
        break;
    case 1032:
        ref->sigFromSrams_bore_69_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_69_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_69_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_69_ram_aux_ckbp)>(scalar);
        break;
    case 1033:
        ref->sigFromSrams_bore_69_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_69_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_69_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_69_ram_mcp_hold)>(scalar);
        break;
    case 1034:
        ref->sigFromSrams_bore_69_cgen = as_scalar<decltype(ref->sigFromSrams_bore_69_cgen)>(scalar);
        wolf->sigFromSrams_bore_69_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_69_cgen)>(scalar);
        break;
    case 1035:
        ref->sigFromSrams_bore_70_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_70_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_70_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_70_ram_hold)>(scalar);
        break;
    case 1036:
        ref->sigFromSrams_bore_70_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_70_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_70_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_70_ram_bypass)>(scalar);
        break;
    case 1037:
        ref->sigFromSrams_bore_70_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_70_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_70_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_70_ram_bp_clken)>(scalar);
        break;
    case 1038:
        ref->sigFromSrams_bore_70_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_70_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_70_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_70_ram_aux_clk)>(scalar);
        break;
    case 1039:
        ref->sigFromSrams_bore_70_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_70_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_70_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_70_ram_aux_ckbp)>(scalar);
        break;
    case 1040:
        ref->sigFromSrams_bore_70_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_70_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_70_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_70_ram_mcp_hold)>(scalar);
        break;
    case 1041:
        ref->sigFromSrams_bore_70_cgen = as_scalar<decltype(ref->sigFromSrams_bore_70_cgen)>(scalar);
        wolf->sigFromSrams_bore_70_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_70_cgen)>(scalar);
        break;
    case 1042:
        ref->sigFromSrams_bore_71_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_71_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_71_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_71_ram_hold)>(scalar);
        break;
    case 1043:
        ref->sigFromSrams_bore_71_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_71_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_71_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_71_ram_bypass)>(scalar);
        break;
    case 1044:
        ref->sigFromSrams_bore_71_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_71_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_71_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_71_ram_bp_clken)>(scalar);
        break;
    case 1045:
        ref->sigFromSrams_bore_71_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_71_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_71_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_71_ram_aux_clk)>(scalar);
        break;
    case 1046:
        ref->sigFromSrams_bore_71_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_71_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_71_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_71_ram_aux_ckbp)>(scalar);
        break;
    case 1047:
        ref->sigFromSrams_bore_71_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_71_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_71_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_71_ram_mcp_hold)>(scalar);
        break;
    case 1048:
        ref->sigFromSrams_bore_71_cgen = as_scalar<decltype(ref->sigFromSrams_bore_71_cgen)>(scalar);
        wolf->sigFromSrams_bore_71_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_71_cgen)>(scalar);
        break;
    case 1049:
        ref->sigFromSrams_bore_72_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_72_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_72_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_72_ram_hold)>(scalar);
        break;
    case 1050:
        ref->sigFromSrams_bore_72_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_72_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_72_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_72_ram_bypass)>(scalar);
        break;
    case 1051:
        ref->sigFromSrams_bore_72_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_72_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_72_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_72_ram_bp_clken)>(scalar);
        break;
    case 1052:
        ref->sigFromSrams_bore_72_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_72_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_72_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_72_ram_aux_clk)>(scalar);
        break;
    case 1053:
        ref->sigFromSrams_bore_72_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_72_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_72_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_72_ram_aux_ckbp)>(scalar);
        break;
    case 1054:
        ref->sigFromSrams_bore_72_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_72_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_72_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_72_ram_mcp_hold)>(scalar);
        break;
    case 1055:
        ref->sigFromSrams_bore_72_cgen = as_scalar<decltype(ref->sigFromSrams_bore_72_cgen)>(scalar);
        wolf->sigFromSrams_bore_72_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_72_cgen)>(scalar);
        break;
    case 1056:
        ref->sigFromSrams_bore_73_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_73_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_73_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_73_ram_hold)>(scalar);
        break;
    case 1057:
        ref->sigFromSrams_bore_73_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_73_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_73_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_73_ram_bypass)>(scalar);
        break;
    case 1058:
        ref->sigFromSrams_bore_73_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_73_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_73_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_73_ram_bp_clken)>(scalar);
        break;
    case 1059:
        ref->sigFromSrams_bore_73_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_73_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_73_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_73_ram_aux_clk)>(scalar);
        break;
    case 1060:
        ref->sigFromSrams_bore_73_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_73_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_73_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_73_ram_aux_ckbp)>(scalar);
        break;
    case 1061:
        ref->sigFromSrams_bore_73_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_73_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_73_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_73_ram_mcp_hold)>(scalar);
        break;
    case 1062:
        ref->sigFromSrams_bore_73_cgen = as_scalar<decltype(ref->sigFromSrams_bore_73_cgen)>(scalar);
        wolf->sigFromSrams_bore_73_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_73_cgen)>(scalar);
        break;
    case 1063:
        ref->sigFromSrams_bore_74_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_74_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_74_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_74_ram_hold)>(scalar);
        break;
    case 1064:
        ref->sigFromSrams_bore_74_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_74_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_74_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_74_ram_bypass)>(scalar);
        break;
    case 1065:
        ref->sigFromSrams_bore_74_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_74_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_74_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_74_ram_bp_clken)>(scalar);
        break;
    case 1066:
        ref->sigFromSrams_bore_74_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_74_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_74_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_74_ram_aux_clk)>(scalar);
        break;
    case 1067:
        ref->sigFromSrams_bore_74_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_74_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_74_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_74_ram_aux_ckbp)>(scalar);
        break;
    case 1068:
        ref->sigFromSrams_bore_74_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_74_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_74_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_74_ram_mcp_hold)>(scalar);
        break;
    case 1069:
        ref->sigFromSrams_bore_74_cgen = as_scalar<decltype(ref->sigFromSrams_bore_74_cgen)>(scalar);
        wolf->sigFromSrams_bore_74_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_74_cgen)>(scalar);
        break;
    case 1070:
        ref->sigFromSrams_bore_75_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_75_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_75_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_75_ram_hold)>(scalar);
        break;
    case 1071:
        ref->sigFromSrams_bore_75_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_75_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_75_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_75_ram_bypass)>(scalar);
        break;
    case 1072:
        ref->sigFromSrams_bore_75_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_75_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_75_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_75_ram_bp_clken)>(scalar);
        break;
    case 1073:
        ref->sigFromSrams_bore_75_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_75_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_75_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_75_ram_aux_clk)>(scalar);
        break;
    case 1074:
        ref->sigFromSrams_bore_75_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_75_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_75_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_75_ram_aux_ckbp)>(scalar);
        break;
    case 1075:
        ref->sigFromSrams_bore_75_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_75_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_75_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_75_ram_mcp_hold)>(scalar);
        break;
    case 1076:
        ref->sigFromSrams_bore_75_cgen = as_scalar<decltype(ref->sigFromSrams_bore_75_cgen)>(scalar);
        wolf->sigFromSrams_bore_75_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_75_cgen)>(scalar);
        break;
    case 1077:
        ref->sigFromSrams_bore_76_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_76_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_76_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_76_ram_hold)>(scalar);
        break;
    case 1078:
        ref->sigFromSrams_bore_76_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_76_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_76_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_76_ram_bypass)>(scalar);
        break;
    case 1079:
        ref->sigFromSrams_bore_76_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_76_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_76_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_76_ram_bp_clken)>(scalar);
        break;
    case 1080:
        ref->sigFromSrams_bore_76_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_76_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_76_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_76_ram_aux_clk)>(scalar);
        break;
    case 1081:
        ref->sigFromSrams_bore_76_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_76_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_76_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_76_ram_aux_ckbp)>(scalar);
        break;
    case 1082:
        ref->sigFromSrams_bore_76_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_76_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_76_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_76_ram_mcp_hold)>(scalar);
        break;
    case 1083:
        ref->sigFromSrams_bore_76_cgen = as_scalar<decltype(ref->sigFromSrams_bore_76_cgen)>(scalar);
        wolf->sigFromSrams_bore_76_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_76_cgen)>(scalar);
        break;
    case 1084:
        ref->sigFromSrams_bore_77_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_77_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_77_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_77_ram_hold)>(scalar);
        break;
    case 1085:
        ref->sigFromSrams_bore_77_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_77_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_77_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_77_ram_bypass)>(scalar);
        break;
    case 1086:
        ref->sigFromSrams_bore_77_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_77_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_77_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_77_ram_bp_clken)>(scalar);
        break;
    case 1087:
        ref->sigFromSrams_bore_77_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_77_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_77_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_77_ram_aux_clk)>(scalar);
        break;
    case 1088:
        ref->sigFromSrams_bore_77_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_77_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_77_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_77_ram_aux_ckbp)>(scalar);
        break;
    case 1089:
        ref->sigFromSrams_bore_77_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_77_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_77_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_77_ram_mcp_hold)>(scalar);
        break;
    case 1090:
        ref->sigFromSrams_bore_77_cgen = as_scalar<decltype(ref->sigFromSrams_bore_77_cgen)>(scalar);
        wolf->sigFromSrams_bore_77_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_77_cgen)>(scalar);
        break;
    case 1091:
        ref->sigFromSrams_bore_78_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_78_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_78_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_78_ram_hold)>(scalar);
        break;
    case 1092:
        ref->sigFromSrams_bore_78_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_78_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_78_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_78_ram_bypass)>(scalar);
        break;
    case 1093:
        ref->sigFromSrams_bore_78_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_78_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_78_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_78_ram_bp_clken)>(scalar);
        break;
    case 1094:
        ref->sigFromSrams_bore_78_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_78_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_78_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_78_ram_aux_clk)>(scalar);
        break;
    case 1095:
        ref->sigFromSrams_bore_78_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_78_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_78_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_78_ram_aux_ckbp)>(scalar);
        break;
    case 1096:
        ref->sigFromSrams_bore_78_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_78_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_78_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_78_ram_mcp_hold)>(scalar);
        break;
    case 1097:
        ref->sigFromSrams_bore_78_cgen = as_scalar<decltype(ref->sigFromSrams_bore_78_cgen)>(scalar);
        wolf->sigFromSrams_bore_78_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_78_cgen)>(scalar);
        break;
    case 1098:
        ref->sigFromSrams_bore_79_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_79_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_79_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_79_ram_hold)>(scalar);
        break;
    case 1099:
        ref->sigFromSrams_bore_79_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_79_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_79_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_79_ram_bypass)>(scalar);
        break;
    case 1100:
        ref->sigFromSrams_bore_79_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_79_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_79_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_79_ram_bp_clken)>(scalar);
        break;
    case 1101:
        ref->sigFromSrams_bore_79_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_79_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_79_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_79_ram_aux_clk)>(scalar);
        break;
    case 1102:
        ref->sigFromSrams_bore_79_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_79_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_79_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_79_ram_aux_ckbp)>(scalar);
        break;
    case 1103:
        ref->sigFromSrams_bore_79_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_79_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_79_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_79_ram_mcp_hold)>(scalar);
        break;
    case 1104:
        ref->sigFromSrams_bore_79_cgen = as_scalar<decltype(ref->sigFromSrams_bore_79_cgen)>(scalar);
        wolf->sigFromSrams_bore_79_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_79_cgen)>(scalar);
        break;
    case 1105:
        ref->sigFromSrams_bore_80_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_80_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_80_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_80_ram_hold)>(scalar);
        break;
    case 1106:
        ref->sigFromSrams_bore_80_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_80_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_80_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_80_ram_bypass)>(scalar);
        break;
    case 1107:
        ref->sigFromSrams_bore_80_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_80_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_80_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_80_ram_bp_clken)>(scalar);
        break;
    case 1108:
        ref->sigFromSrams_bore_80_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_80_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_80_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_80_ram_aux_clk)>(scalar);
        break;
    case 1109:
        ref->sigFromSrams_bore_80_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_80_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_80_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_80_ram_aux_ckbp)>(scalar);
        break;
    case 1110:
        ref->sigFromSrams_bore_80_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_80_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_80_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_80_ram_mcp_hold)>(scalar);
        break;
    case 1111:
        ref->sigFromSrams_bore_80_cgen = as_scalar<decltype(ref->sigFromSrams_bore_80_cgen)>(scalar);
        wolf->sigFromSrams_bore_80_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_80_cgen)>(scalar);
        break;
    case 1112:
        ref->sigFromSrams_bore_81_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_81_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_81_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_81_ram_hold)>(scalar);
        break;
    case 1113:
        ref->sigFromSrams_bore_81_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_81_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_81_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_81_ram_bypass)>(scalar);
        break;
    case 1114:
        ref->sigFromSrams_bore_81_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_81_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_81_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_81_ram_bp_clken)>(scalar);
        break;
    case 1115:
        ref->sigFromSrams_bore_81_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_81_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_81_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_81_ram_aux_clk)>(scalar);
        break;
    case 1116:
        ref->sigFromSrams_bore_81_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_81_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_81_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_81_ram_aux_ckbp)>(scalar);
        break;
    case 1117:
        ref->sigFromSrams_bore_81_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_81_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_81_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_81_ram_mcp_hold)>(scalar);
        break;
    case 1118:
        ref->sigFromSrams_bore_81_cgen = as_scalar<decltype(ref->sigFromSrams_bore_81_cgen)>(scalar);
        wolf->sigFromSrams_bore_81_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_81_cgen)>(scalar);
        break;
    case 1119:
        ref->sigFromSrams_bore_82_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_82_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_82_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_82_ram_hold)>(scalar);
        break;
    case 1120:
        ref->sigFromSrams_bore_82_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_82_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_82_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_82_ram_bypass)>(scalar);
        break;
    case 1121:
        ref->sigFromSrams_bore_82_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_82_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_82_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_82_ram_bp_clken)>(scalar);
        break;
    case 1122:
        ref->sigFromSrams_bore_82_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_82_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_82_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_82_ram_aux_clk)>(scalar);
        break;
    case 1123:
        ref->sigFromSrams_bore_82_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_82_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_82_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_82_ram_aux_ckbp)>(scalar);
        break;
    case 1124:
        ref->sigFromSrams_bore_82_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_82_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_82_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_82_ram_mcp_hold)>(scalar);
        break;
    case 1125:
        ref->sigFromSrams_bore_82_cgen = as_scalar<decltype(ref->sigFromSrams_bore_82_cgen)>(scalar);
        wolf->sigFromSrams_bore_82_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_82_cgen)>(scalar);
        break;
    case 1126:
        ref->sigFromSrams_bore_83_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_83_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_83_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_83_ram_hold)>(scalar);
        break;
    case 1127:
        ref->sigFromSrams_bore_83_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_83_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_83_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_83_ram_bypass)>(scalar);
        break;
    case 1128:
        ref->sigFromSrams_bore_83_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_83_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_83_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_83_ram_bp_clken)>(scalar);
        break;
    case 1129:
        ref->sigFromSrams_bore_83_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_83_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_83_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_83_ram_aux_clk)>(scalar);
        break;
    case 1130:
        ref->sigFromSrams_bore_83_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_83_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_83_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_83_ram_aux_ckbp)>(scalar);
        break;
    case 1131:
        ref->sigFromSrams_bore_83_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_83_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_83_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_83_ram_mcp_hold)>(scalar);
        break;
    case 1132:
        ref->sigFromSrams_bore_83_cgen = as_scalar<decltype(ref->sigFromSrams_bore_83_cgen)>(scalar);
        wolf->sigFromSrams_bore_83_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_83_cgen)>(scalar);
        break;
    case 1133:
        ref->sigFromSrams_bore_84_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_84_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_84_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_84_ram_hold)>(scalar);
        break;
    case 1134:
        ref->sigFromSrams_bore_84_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_84_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_84_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_84_ram_bypass)>(scalar);
        break;
    case 1135:
        ref->sigFromSrams_bore_84_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_84_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_84_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_84_ram_bp_clken)>(scalar);
        break;
    case 1136:
        ref->sigFromSrams_bore_84_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_84_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_84_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_84_ram_aux_clk)>(scalar);
        break;
    case 1137:
        ref->sigFromSrams_bore_84_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_84_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_84_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_84_ram_aux_ckbp)>(scalar);
        break;
    case 1138:
        ref->sigFromSrams_bore_84_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_84_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_84_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_84_ram_mcp_hold)>(scalar);
        break;
    case 1139:
        ref->sigFromSrams_bore_84_cgen = as_scalar<decltype(ref->sigFromSrams_bore_84_cgen)>(scalar);
        wolf->sigFromSrams_bore_84_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_84_cgen)>(scalar);
        break;
    case 1140:
        ref->sigFromSrams_bore_85_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_85_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_85_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_85_ram_hold)>(scalar);
        break;
    case 1141:
        ref->sigFromSrams_bore_85_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_85_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_85_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_85_ram_bypass)>(scalar);
        break;
    case 1142:
        ref->sigFromSrams_bore_85_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_85_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_85_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_85_ram_bp_clken)>(scalar);
        break;
    case 1143:
        ref->sigFromSrams_bore_85_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_85_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_85_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_85_ram_aux_clk)>(scalar);
        break;
    case 1144:
        ref->sigFromSrams_bore_85_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_85_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_85_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_85_ram_aux_ckbp)>(scalar);
        break;
    case 1145:
        ref->sigFromSrams_bore_85_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_85_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_85_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_85_ram_mcp_hold)>(scalar);
        break;
    case 1146:
        ref->sigFromSrams_bore_85_cgen = as_scalar<decltype(ref->sigFromSrams_bore_85_cgen)>(scalar);
        wolf->sigFromSrams_bore_85_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_85_cgen)>(scalar);
        break;
    case 1147:
        ref->sigFromSrams_bore_86_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_86_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_86_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_86_ram_hold)>(scalar);
        break;
    case 1148:
        ref->sigFromSrams_bore_86_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_86_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_86_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_86_ram_bypass)>(scalar);
        break;
    case 1149:
        ref->sigFromSrams_bore_86_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_86_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_86_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_86_ram_bp_clken)>(scalar);
        break;
    case 1150:
        ref->sigFromSrams_bore_86_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_86_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_86_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_86_ram_aux_clk)>(scalar);
        break;
    case 1151:
        ref->sigFromSrams_bore_86_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_86_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_86_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_86_ram_aux_ckbp)>(scalar);
        break;
    case 1152:
        ref->sigFromSrams_bore_86_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_86_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_86_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_86_ram_mcp_hold)>(scalar);
        break;
    case 1153:
        ref->sigFromSrams_bore_86_cgen = as_scalar<decltype(ref->sigFromSrams_bore_86_cgen)>(scalar);
        wolf->sigFromSrams_bore_86_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_86_cgen)>(scalar);
        break;
    case 1154:
        ref->sigFromSrams_bore_87_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_87_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_87_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_87_ram_hold)>(scalar);
        break;
    case 1155:
        ref->sigFromSrams_bore_87_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_87_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_87_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_87_ram_bypass)>(scalar);
        break;
    case 1156:
        ref->sigFromSrams_bore_87_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_87_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_87_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_87_ram_bp_clken)>(scalar);
        break;
    case 1157:
        ref->sigFromSrams_bore_87_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_87_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_87_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_87_ram_aux_clk)>(scalar);
        break;
    case 1158:
        ref->sigFromSrams_bore_87_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_87_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_87_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_87_ram_aux_ckbp)>(scalar);
        break;
    case 1159:
        ref->sigFromSrams_bore_87_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_87_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_87_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_87_ram_mcp_hold)>(scalar);
        break;
    case 1160:
        ref->sigFromSrams_bore_87_cgen = as_scalar<decltype(ref->sigFromSrams_bore_87_cgen)>(scalar);
        wolf->sigFromSrams_bore_87_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_87_cgen)>(scalar);
        break;
    case 1161:
        ref->sigFromSrams_bore_88_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_88_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_88_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_88_ram_hold)>(scalar);
        break;
    case 1162:
        ref->sigFromSrams_bore_88_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_88_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_88_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_88_ram_bypass)>(scalar);
        break;
    case 1163:
        ref->sigFromSrams_bore_88_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_88_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_88_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_88_ram_bp_clken)>(scalar);
        break;
    case 1164:
        ref->sigFromSrams_bore_88_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_88_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_88_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_88_ram_aux_clk)>(scalar);
        break;
    case 1165:
        ref->sigFromSrams_bore_88_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_88_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_88_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_88_ram_aux_ckbp)>(scalar);
        break;
    case 1166:
        ref->sigFromSrams_bore_88_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_88_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_88_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_88_ram_mcp_hold)>(scalar);
        break;
    case 1167:
        ref->sigFromSrams_bore_88_cgen = as_scalar<decltype(ref->sigFromSrams_bore_88_cgen)>(scalar);
        wolf->sigFromSrams_bore_88_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_88_cgen)>(scalar);
        break;
    case 1168:
        ref->sigFromSrams_bore_89_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_89_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_89_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_89_ram_hold)>(scalar);
        break;
    case 1169:
        ref->sigFromSrams_bore_89_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_89_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_89_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_89_ram_bypass)>(scalar);
        break;
    case 1170:
        ref->sigFromSrams_bore_89_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_89_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_89_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_89_ram_bp_clken)>(scalar);
        break;
    case 1171:
        ref->sigFromSrams_bore_89_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_89_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_89_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_89_ram_aux_clk)>(scalar);
        break;
    case 1172:
        ref->sigFromSrams_bore_89_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_89_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_89_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_89_ram_aux_ckbp)>(scalar);
        break;
    case 1173:
        ref->sigFromSrams_bore_89_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_89_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_89_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_89_ram_mcp_hold)>(scalar);
        break;
    case 1174:
        ref->sigFromSrams_bore_89_cgen = as_scalar<decltype(ref->sigFromSrams_bore_89_cgen)>(scalar);
        wolf->sigFromSrams_bore_89_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_89_cgen)>(scalar);
        break;
    case 1175:
        ref->sigFromSrams_bore_90_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_90_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_90_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_90_ram_hold)>(scalar);
        break;
    case 1176:
        ref->sigFromSrams_bore_90_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_90_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_90_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_90_ram_bypass)>(scalar);
        break;
    case 1177:
        ref->sigFromSrams_bore_90_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_90_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_90_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_90_ram_bp_clken)>(scalar);
        break;
    case 1178:
        ref->sigFromSrams_bore_90_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_90_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_90_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_90_ram_aux_clk)>(scalar);
        break;
    case 1179:
        ref->sigFromSrams_bore_90_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_90_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_90_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_90_ram_aux_ckbp)>(scalar);
        break;
    case 1180:
        ref->sigFromSrams_bore_90_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_90_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_90_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_90_ram_mcp_hold)>(scalar);
        break;
    case 1181:
        ref->sigFromSrams_bore_90_cgen = as_scalar<decltype(ref->sigFromSrams_bore_90_cgen)>(scalar);
        wolf->sigFromSrams_bore_90_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_90_cgen)>(scalar);
        break;
    case 1182:
        ref->sigFromSrams_bore_91_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_91_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_91_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_91_ram_hold)>(scalar);
        break;
    case 1183:
        ref->sigFromSrams_bore_91_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_91_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_91_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_91_ram_bypass)>(scalar);
        break;
    case 1184:
        ref->sigFromSrams_bore_91_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_91_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_91_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_91_ram_bp_clken)>(scalar);
        break;
    case 1185:
        ref->sigFromSrams_bore_91_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_91_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_91_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_91_ram_aux_clk)>(scalar);
        break;
    case 1186:
        ref->sigFromSrams_bore_91_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_91_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_91_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_91_ram_aux_ckbp)>(scalar);
        break;
    case 1187:
        ref->sigFromSrams_bore_91_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_91_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_91_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_91_ram_mcp_hold)>(scalar);
        break;
    case 1188:
        ref->sigFromSrams_bore_91_cgen = as_scalar<decltype(ref->sigFromSrams_bore_91_cgen)>(scalar);
        wolf->sigFromSrams_bore_91_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_91_cgen)>(scalar);
        break;
    case 1189:
        ref->sigFromSrams_bore_92_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_92_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_92_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_92_ram_hold)>(scalar);
        break;
    case 1190:
        ref->sigFromSrams_bore_92_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_92_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_92_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_92_ram_bypass)>(scalar);
        break;
    case 1191:
        ref->sigFromSrams_bore_92_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_92_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_92_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_92_ram_bp_clken)>(scalar);
        break;
    case 1192:
        ref->sigFromSrams_bore_92_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_92_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_92_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_92_ram_aux_clk)>(scalar);
        break;
    case 1193:
        ref->sigFromSrams_bore_92_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_92_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_92_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_92_ram_aux_ckbp)>(scalar);
        break;
    case 1194:
        ref->sigFromSrams_bore_92_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_92_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_92_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_92_ram_mcp_hold)>(scalar);
        break;
    case 1195:
        ref->sigFromSrams_bore_92_cgen = as_scalar<decltype(ref->sigFromSrams_bore_92_cgen)>(scalar);
        wolf->sigFromSrams_bore_92_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_92_cgen)>(scalar);
        break;
    case 1196:
        ref->sigFromSrams_bore_93_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_93_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_93_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_93_ram_hold)>(scalar);
        break;
    case 1197:
        ref->sigFromSrams_bore_93_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_93_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_93_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_93_ram_bypass)>(scalar);
        break;
    case 1198:
        ref->sigFromSrams_bore_93_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_93_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_93_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_93_ram_bp_clken)>(scalar);
        break;
    case 1199:
        ref->sigFromSrams_bore_93_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_93_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_93_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_93_ram_aux_clk)>(scalar);
        break;
    case 1200:
        ref->sigFromSrams_bore_93_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_93_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_93_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_93_ram_aux_ckbp)>(scalar);
        break;
    case 1201:
        ref->sigFromSrams_bore_93_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_93_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_93_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_93_ram_mcp_hold)>(scalar);
        break;
    case 1202:
        ref->sigFromSrams_bore_93_cgen = as_scalar<decltype(ref->sigFromSrams_bore_93_cgen)>(scalar);
        wolf->sigFromSrams_bore_93_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_93_cgen)>(scalar);
        break;
    case 1203:
        ref->sigFromSrams_bore_94_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_94_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_94_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_94_ram_hold)>(scalar);
        break;
    case 1204:
        ref->sigFromSrams_bore_94_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_94_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_94_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_94_ram_bypass)>(scalar);
        break;
    case 1205:
        ref->sigFromSrams_bore_94_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_94_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_94_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_94_ram_bp_clken)>(scalar);
        break;
    case 1206:
        ref->sigFromSrams_bore_94_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_94_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_94_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_94_ram_aux_clk)>(scalar);
        break;
    case 1207:
        ref->sigFromSrams_bore_94_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_94_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_94_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_94_ram_aux_ckbp)>(scalar);
        break;
    case 1208:
        ref->sigFromSrams_bore_94_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_94_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_94_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_94_ram_mcp_hold)>(scalar);
        break;
    case 1209:
        ref->sigFromSrams_bore_94_cgen = as_scalar<decltype(ref->sigFromSrams_bore_94_cgen)>(scalar);
        wolf->sigFromSrams_bore_94_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_94_cgen)>(scalar);
        break;
    case 1210:
        ref->sigFromSrams_bore_95_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_95_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_95_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_95_ram_hold)>(scalar);
        break;
    case 1211:
        ref->sigFromSrams_bore_95_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_95_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_95_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_95_ram_bypass)>(scalar);
        break;
    case 1212:
        ref->sigFromSrams_bore_95_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_95_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_95_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_95_ram_bp_clken)>(scalar);
        break;
    case 1213:
        ref->sigFromSrams_bore_95_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_95_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_95_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_95_ram_aux_clk)>(scalar);
        break;
    case 1214:
        ref->sigFromSrams_bore_95_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_95_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_95_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_95_ram_aux_ckbp)>(scalar);
        break;
    case 1215:
        ref->sigFromSrams_bore_95_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_95_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_95_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_95_ram_mcp_hold)>(scalar);
        break;
    case 1216:
        ref->sigFromSrams_bore_95_cgen = as_scalar<decltype(ref->sigFromSrams_bore_95_cgen)>(scalar);
        wolf->sigFromSrams_bore_95_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_95_cgen)>(scalar);
        break;
    case 1217:
        ref->sigFromSrams_bore_96_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_96_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_96_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_96_ram_hold)>(scalar);
        break;
    case 1218:
        ref->sigFromSrams_bore_96_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_96_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_96_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_96_ram_bypass)>(scalar);
        break;
    case 1219:
        ref->sigFromSrams_bore_96_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_96_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_96_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_96_ram_bp_clken)>(scalar);
        break;
    case 1220:
        ref->sigFromSrams_bore_96_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_96_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_96_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_96_ram_aux_clk)>(scalar);
        break;
    case 1221:
        ref->sigFromSrams_bore_96_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_96_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_96_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_96_ram_aux_ckbp)>(scalar);
        break;
    case 1222:
        ref->sigFromSrams_bore_96_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_96_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_96_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_96_ram_mcp_hold)>(scalar);
        break;
    case 1223:
        ref->sigFromSrams_bore_96_cgen = as_scalar<decltype(ref->sigFromSrams_bore_96_cgen)>(scalar);
        wolf->sigFromSrams_bore_96_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_96_cgen)>(scalar);
        break;
    case 1224:
        ref->sigFromSrams_bore_97_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_97_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_97_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_97_ram_hold)>(scalar);
        break;
    case 1225:
        ref->sigFromSrams_bore_97_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_97_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_97_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_97_ram_bypass)>(scalar);
        break;
    case 1226:
        ref->sigFromSrams_bore_97_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_97_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_97_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_97_ram_bp_clken)>(scalar);
        break;
    case 1227:
        ref->sigFromSrams_bore_97_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_97_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_97_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_97_ram_aux_clk)>(scalar);
        break;
    case 1228:
        ref->sigFromSrams_bore_97_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_97_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_97_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_97_ram_aux_ckbp)>(scalar);
        break;
    case 1229:
        ref->sigFromSrams_bore_97_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_97_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_97_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_97_ram_mcp_hold)>(scalar);
        break;
    case 1230:
        ref->sigFromSrams_bore_97_cgen = as_scalar<decltype(ref->sigFromSrams_bore_97_cgen)>(scalar);
        wolf->sigFromSrams_bore_97_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_97_cgen)>(scalar);
        break;
    case 1231:
        ref->sigFromSrams_bore_98_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_98_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_98_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_98_ram_hold)>(scalar);
        break;
    case 1232:
        ref->sigFromSrams_bore_98_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_98_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_98_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_98_ram_bypass)>(scalar);
        break;
    case 1233:
        ref->sigFromSrams_bore_98_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_98_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_98_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_98_ram_bp_clken)>(scalar);
        break;
    case 1234:
        ref->sigFromSrams_bore_98_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_98_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_98_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_98_ram_aux_clk)>(scalar);
        break;
    case 1235:
        ref->sigFromSrams_bore_98_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_98_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_98_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_98_ram_aux_ckbp)>(scalar);
        break;
    case 1236:
        ref->sigFromSrams_bore_98_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_98_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_98_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_98_ram_mcp_hold)>(scalar);
        break;
    case 1237:
        ref->sigFromSrams_bore_98_cgen = as_scalar<decltype(ref->sigFromSrams_bore_98_cgen)>(scalar);
        wolf->sigFromSrams_bore_98_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_98_cgen)>(scalar);
        break;
    case 1238:
        ref->sigFromSrams_bore_99_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_99_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_99_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_99_ram_hold)>(scalar);
        break;
    case 1239:
        ref->sigFromSrams_bore_99_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_99_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_99_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_99_ram_bypass)>(scalar);
        break;
    case 1240:
        ref->sigFromSrams_bore_99_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_99_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_99_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_99_ram_bp_clken)>(scalar);
        break;
    case 1241:
        ref->sigFromSrams_bore_99_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_99_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_99_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_99_ram_aux_clk)>(scalar);
        break;
    case 1242:
        ref->sigFromSrams_bore_99_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_99_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_99_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_99_ram_aux_ckbp)>(scalar);
        break;
    case 1243:
        ref->sigFromSrams_bore_99_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_99_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_99_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_99_ram_mcp_hold)>(scalar);
        break;
    case 1244:
        ref->sigFromSrams_bore_99_cgen = as_scalar<decltype(ref->sigFromSrams_bore_99_cgen)>(scalar);
        wolf->sigFromSrams_bore_99_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_99_cgen)>(scalar);
        break;
    case 1245:
        ref->sigFromSrams_bore_100_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_100_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_100_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_100_ram_hold)>(scalar);
        break;
    case 1246:
        ref->sigFromSrams_bore_100_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_100_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_100_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_100_ram_bypass)>(scalar);
        break;
    case 1247:
        ref->sigFromSrams_bore_100_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_100_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_100_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_100_ram_bp_clken)>(scalar);
        break;
    case 1248:
        ref->sigFromSrams_bore_100_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_100_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_100_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_100_ram_aux_clk)>(scalar);
        break;
    case 1249:
        ref->sigFromSrams_bore_100_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_100_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_100_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_100_ram_aux_ckbp)>(scalar);
        break;
    case 1250:
        ref->sigFromSrams_bore_100_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_100_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_100_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_100_ram_mcp_hold)>(scalar);
        break;
    case 1251:
        ref->sigFromSrams_bore_100_cgen = as_scalar<decltype(ref->sigFromSrams_bore_100_cgen)>(scalar);
        wolf->sigFromSrams_bore_100_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_100_cgen)>(scalar);
        break;
    case 1252:
        ref->sigFromSrams_bore_101_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_101_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_101_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_101_ram_hold)>(scalar);
        break;
    case 1253:
        ref->sigFromSrams_bore_101_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_101_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_101_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_101_ram_bypass)>(scalar);
        break;
    case 1254:
        ref->sigFromSrams_bore_101_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_101_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_101_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_101_ram_bp_clken)>(scalar);
        break;
    case 1255:
        ref->sigFromSrams_bore_101_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_101_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_101_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_101_ram_aux_clk)>(scalar);
        break;
    case 1256:
        ref->sigFromSrams_bore_101_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_101_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_101_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_101_ram_aux_ckbp)>(scalar);
        break;
    case 1257:
        ref->sigFromSrams_bore_101_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_101_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_101_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_101_ram_mcp_hold)>(scalar);
        break;
    case 1258:
        ref->sigFromSrams_bore_101_cgen = as_scalar<decltype(ref->sigFromSrams_bore_101_cgen)>(scalar);
        wolf->sigFromSrams_bore_101_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_101_cgen)>(scalar);
        break;
    case 1259:
        ref->sigFromSrams_bore_102_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_102_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_102_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_102_ram_hold)>(scalar);
        break;
    case 1260:
        ref->sigFromSrams_bore_102_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_102_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_102_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_102_ram_bypass)>(scalar);
        break;
    case 1261:
        ref->sigFromSrams_bore_102_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_102_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_102_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_102_ram_bp_clken)>(scalar);
        break;
    case 1262:
        ref->sigFromSrams_bore_102_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_102_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_102_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_102_ram_aux_clk)>(scalar);
        break;
    case 1263:
        ref->sigFromSrams_bore_102_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_102_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_102_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_102_ram_aux_ckbp)>(scalar);
        break;
    case 1264:
        ref->sigFromSrams_bore_102_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_102_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_102_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_102_ram_mcp_hold)>(scalar);
        break;
    case 1265:
        ref->sigFromSrams_bore_102_cgen = as_scalar<decltype(ref->sigFromSrams_bore_102_cgen)>(scalar);
        wolf->sigFromSrams_bore_102_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_102_cgen)>(scalar);
        break;
    case 1266:
        ref->sigFromSrams_bore_103_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_103_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_103_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_103_ram_hold)>(scalar);
        break;
    case 1267:
        ref->sigFromSrams_bore_103_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_103_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_103_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_103_ram_bypass)>(scalar);
        break;
    case 1268:
        ref->sigFromSrams_bore_103_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_103_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_103_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_103_ram_bp_clken)>(scalar);
        break;
    case 1269:
        ref->sigFromSrams_bore_103_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_103_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_103_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_103_ram_aux_clk)>(scalar);
        break;
    case 1270:
        ref->sigFromSrams_bore_103_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_103_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_103_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_103_ram_aux_ckbp)>(scalar);
        break;
    case 1271:
        ref->sigFromSrams_bore_103_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_103_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_103_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_103_ram_mcp_hold)>(scalar);
        break;
    case 1272:
        ref->sigFromSrams_bore_103_cgen = as_scalar<decltype(ref->sigFromSrams_bore_103_cgen)>(scalar);
        wolf->sigFromSrams_bore_103_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_103_cgen)>(scalar);
        break;
    case 1273:
        ref->sigFromSrams_bore_104_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_104_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_104_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_104_ram_hold)>(scalar);
        break;
    case 1274:
        ref->sigFromSrams_bore_104_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_104_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_104_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_104_ram_bypass)>(scalar);
        break;
    case 1275:
        ref->sigFromSrams_bore_104_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_104_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_104_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_104_ram_bp_clken)>(scalar);
        break;
    case 1276:
        ref->sigFromSrams_bore_104_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_104_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_104_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_104_ram_aux_clk)>(scalar);
        break;
    case 1277:
        ref->sigFromSrams_bore_104_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_104_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_104_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_104_ram_aux_ckbp)>(scalar);
        break;
    case 1278:
        ref->sigFromSrams_bore_104_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_104_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_104_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_104_ram_mcp_hold)>(scalar);
        break;
    case 1279:
        ref->sigFromSrams_bore_104_cgen = as_scalar<decltype(ref->sigFromSrams_bore_104_cgen)>(scalar);
        wolf->sigFromSrams_bore_104_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_104_cgen)>(scalar);
        break;
    case 1280:
        ref->sigFromSrams_bore_105_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_105_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_105_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_105_ram_hold)>(scalar);
        break;
    case 1281:
        ref->sigFromSrams_bore_105_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_105_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_105_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_105_ram_bypass)>(scalar);
        break;
    case 1282:
        ref->sigFromSrams_bore_105_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_105_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_105_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_105_ram_bp_clken)>(scalar);
        break;
    case 1283:
        ref->sigFromSrams_bore_105_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_105_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_105_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_105_ram_aux_clk)>(scalar);
        break;
    case 1284:
        ref->sigFromSrams_bore_105_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_105_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_105_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_105_ram_aux_ckbp)>(scalar);
        break;
    case 1285:
        ref->sigFromSrams_bore_105_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_105_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_105_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_105_ram_mcp_hold)>(scalar);
        break;
    case 1286:
        ref->sigFromSrams_bore_105_cgen = as_scalar<decltype(ref->sigFromSrams_bore_105_cgen)>(scalar);
        wolf->sigFromSrams_bore_105_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_105_cgen)>(scalar);
        break;
    case 1287:
        ref->sigFromSrams_bore_106_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_106_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_106_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_106_ram_hold)>(scalar);
        break;
    case 1288:
        ref->sigFromSrams_bore_106_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_106_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_106_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_106_ram_bypass)>(scalar);
        break;
    case 1289:
        ref->sigFromSrams_bore_106_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_106_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_106_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_106_ram_bp_clken)>(scalar);
        break;
    case 1290:
        ref->sigFromSrams_bore_106_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_106_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_106_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_106_ram_aux_clk)>(scalar);
        break;
    case 1291:
        ref->sigFromSrams_bore_106_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_106_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_106_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_106_ram_aux_ckbp)>(scalar);
        break;
    case 1292:
        ref->sigFromSrams_bore_106_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_106_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_106_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_106_ram_mcp_hold)>(scalar);
        break;
    case 1293:
        ref->sigFromSrams_bore_106_cgen = as_scalar<decltype(ref->sigFromSrams_bore_106_cgen)>(scalar);
        wolf->sigFromSrams_bore_106_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_106_cgen)>(scalar);
        break;
    case 1294:
        ref->sigFromSrams_bore_107_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_107_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_107_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_107_ram_hold)>(scalar);
        break;
    case 1295:
        ref->sigFromSrams_bore_107_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_107_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_107_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_107_ram_bypass)>(scalar);
        break;
    case 1296:
        ref->sigFromSrams_bore_107_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_107_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_107_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_107_ram_bp_clken)>(scalar);
        break;
    case 1297:
        ref->sigFromSrams_bore_107_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_107_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_107_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_107_ram_aux_clk)>(scalar);
        break;
    case 1298:
        ref->sigFromSrams_bore_107_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_107_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_107_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_107_ram_aux_ckbp)>(scalar);
        break;
    case 1299:
        ref->sigFromSrams_bore_107_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_107_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_107_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_107_ram_mcp_hold)>(scalar);
        break;
    case 1300:
        ref->sigFromSrams_bore_107_cgen = as_scalar<decltype(ref->sigFromSrams_bore_107_cgen)>(scalar);
        wolf->sigFromSrams_bore_107_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_107_cgen)>(scalar);
        break;
    case 1301:
        ref->sigFromSrams_bore_108_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_108_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_108_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_108_ram_hold)>(scalar);
        break;
    case 1302:
        ref->sigFromSrams_bore_108_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_108_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_108_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_108_ram_bypass)>(scalar);
        break;
    case 1303:
        ref->sigFromSrams_bore_108_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_108_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_108_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_108_ram_bp_clken)>(scalar);
        break;
    case 1304:
        ref->sigFromSrams_bore_108_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_108_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_108_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_108_ram_aux_clk)>(scalar);
        break;
    case 1305:
        ref->sigFromSrams_bore_108_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_108_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_108_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_108_ram_aux_ckbp)>(scalar);
        break;
    case 1306:
        ref->sigFromSrams_bore_108_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_108_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_108_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_108_ram_mcp_hold)>(scalar);
        break;
    case 1307:
        ref->sigFromSrams_bore_108_cgen = as_scalar<decltype(ref->sigFromSrams_bore_108_cgen)>(scalar);
        wolf->sigFromSrams_bore_108_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_108_cgen)>(scalar);
        break;
    case 1308:
        ref->sigFromSrams_bore_109_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_109_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_109_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_109_ram_hold)>(scalar);
        break;
    case 1309:
        ref->sigFromSrams_bore_109_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_109_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_109_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_109_ram_bypass)>(scalar);
        break;
    case 1310:
        ref->sigFromSrams_bore_109_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_109_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_109_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_109_ram_bp_clken)>(scalar);
        break;
    case 1311:
        ref->sigFromSrams_bore_109_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_109_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_109_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_109_ram_aux_clk)>(scalar);
        break;
    case 1312:
        ref->sigFromSrams_bore_109_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_109_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_109_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_109_ram_aux_ckbp)>(scalar);
        break;
    case 1313:
        ref->sigFromSrams_bore_109_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_109_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_109_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_109_ram_mcp_hold)>(scalar);
        break;
    case 1314:
        ref->sigFromSrams_bore_109_cgen = as_scalar<decltype(ref->sigFromSrams_bore_109_cgen)>(scalar);
        wolf->sigFromSrams_bore_109_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_109_cgen)>(scalar);
        break;
    case 1315:
        ref->sigFromSrams_bore_110_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_110_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_110_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_110_ram_hold)>(scalar);
        break;
    case 1316:
        ref->sigFromSrams_bore_110_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_110_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_110_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_110_ram_bypass)>(scalar);
        break;
    case 1317:
        ref->sigFromSrams_bore_110_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_110_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_110_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_110_ram_bp_clken)>(scalar);
        break;
    case 1318:
        ref->sigFromSrams_bore_110_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_110_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_110_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_110_ram_aux_clk)>(scalar);
        break;
    case 1319:
        ref->sigFromSrams_bore_110_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_110_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_110_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_110_ram_aux_ckbp)>(scalar);
        break;
    case 1320:
        ref->sigFromSrams_bore_110_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_110_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_110_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_110_ram_mcp_hold)>(scalar);
        break;
    case 1321:
        ref->sigFromSrams_bore_110_cgen = as_scalar<decltype(ref->sigFromSrams_bore_110_cgen)>(scalar);
        wolf->sigFromSrams_bore_110_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_110_cgen)>(scalar);
        break;
    case 1322:
        ref->sigFromSrams_bore_111_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_111_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_111_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_111_ram_hold)>(scalar);
        break;
    case 1323:
        ref->sigFromSrams_bore_111_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_111_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_111_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_111_ram_bypass)>(scalar);
        break;
    case 1324:
        ref->sigFromSrams_bore_111_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_111_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_111_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_111_ram_bp_clken)>(scalar);
        break;
    case 1325:
        ref->sigFromSrams_bore_111_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_111_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_111_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_111_ram_aux_clk)>(scalar);
        break;
    case 1326:
        ref->sigFromSrams_bore_111_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_111_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_111_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_111_ram_aux_ckbp)>(scalar);
        break;
    case 1327:
        ref->sigFromSrams_bore_111_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_111_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_111_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_111_ram_mcp_hold)>(scalar);
        break;
    case 1328:
        ref->sigFromSrams_bore_111_cgen = as_scalar<decltype(ref->sigFromSrams_bore_111_cgen)>(scalar);
        wolf->sigFromSrams_bore_111_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_111_cgen)>(scalar);
        break;
    case 1329:
        ref->sigFromSrams_bore_112_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_112_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_112_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_112_ram_hold)>(scalar);
        break;
    case 1330:
        ref->sigFromSrams_bore_112_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_112_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_112_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_112_ram_bypass)>(scalar);
        break;
    case 1331:
        ref->sigFromSrams_bore_112_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_112_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_112_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_112_ram_bp_clken)>(scalar);
        break;
    case 1332:
        ref->sigFromSrams_bore_112_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_112_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_112_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_112_ram_aux_clk)>(scalar);
        break;
    case 1333:
        ref->sigFromSrams_bore_112_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_112_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_112_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_112_ram_aux_ckbp)>(scalar);
        break;
    case 1334:
        ref->sigFromSrams_bore_112_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_112_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_112_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_112_ram_mcp_hold)>(scalar);
        break;
    case 1335:
        ref->sigFromSrams_bore_112_cgen = as_scalar<decltype(ref->sigFromSrams_bore_112_cgen)>(scalar);
        wolf->sigFromSrams_bore_112_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_112_cgen)>(scalar);
        break;
    case 1336:
        ref->sigFromSrams_bore_113_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_113_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_113_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_113_ram_hold)>(scalar);
        break;
    case 1337:
        ref->sigFromSrams_bore_113_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_113_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_113_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_113_ram_bypass)>(scalar);
        break;
    case 1338:
        ref->sigFromSrams_bore_113_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_113_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_113_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_113_ram_bp_clken)>(scalar);
        break;
    case 1339:
        ref->sigFromSrams_bore_113_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_113_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_113_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_113_ram_aux_clk)>(scalar);
        break;
    case 1340:
        ref->sigFromSrams_bore_113_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_113_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_113_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_113_ram_aux_ckbp)>(scalar);
        break;
    case 1341:
        ref->sigFromSrams_bore_113_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_113_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_113_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_113_ram_mcp_hold)>(scalar);
        break;
    case 1342:
        ref->sigFromSrams_bore_113_cgen = as_scalar<decltype(ref->sigFromSrams_bore_113_cgen)>(scalar);
        wolf->sigFromSrams_bore_113_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_113_cgen)>(scalar);
        break;
    case 1343:
        ref->sigFromSrams_bore_114_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_114_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_114_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_114_ram_hold)>(scalar);
        break;
    case 1344:
        ref->sigFromSrams_bore_114_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_114_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_114_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_114_ram_bypass)>(scalar);
        break;
    case 1345:
        ref->sigFromSrams_bore_114_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_114_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_114_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_114_ram_bp_clken)>(scalar);
        break;
    case 1346:
        ref->sigFromSrams_bore_114_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_114_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_114_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_114_ram_aux_clk)>(scalar);
        break;
    case 1347:
        ref->sigFromSrams_bore_114_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_114_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_114_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_114_ram_aux_ckbp)>(scalar);
        break;
    case 1348:
        ref->sigFromSrams_bore_114_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_114_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_114_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_114_ram_mcp_hold)>(scalar);
        break;
    case 1349:
        ref->sigFromSrams_bore_114_cgen = as_scalar<decltype(ref->sigFromSrams_bore_114_cgen)>(scalar);
        wolf->sigFromSrams_bore_114_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_114_cgen)>(scalar);
        break;
    case 1350:
        ref->sigFromSrams_bore_115_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_115_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_115_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_115_ram_hold)>(scalar);
        break;
    case 1351:
        ref->sigFromSrams_bore_115_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_115_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_115_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_115_ram_bypass)>(scalar);
        break;
    case 1352:
        ref->sigFromSrams_bore_115_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_115_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_115_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_115_ram_bp_clken)>(scalar);
        break;
    case 1353:
        ref->sigFromSrams_bore_115_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_115_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_115_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_115_ram_aux_clk)>(scalar);
        break;
    case 1354:
        ref->sigFromSrams_bore_115_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_115_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_115_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_115_ram_aux_ckbp)>(scalar);
        break;
    case 1355:
        ref->sigFromSrams_bore_115_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_115_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_115_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_115_ram_mcp_hold)>(scalar);
        break;
    case 1356:
        ref->sigFromSrams_bore_115_cgen = as_scalar<decltype(ref->sigFromSrams_bore_115_cgen)>(scalar);
        wolf->sigFromSrams_bore_115_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_115_cgen)>(scalar);
        break;
    case 1357:
        ref->sigFromSrams_bore_116_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_116_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_116_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_116_ram_hold)>(scalar);
        break;
    case 1358:
        ref->sigFromSrams_bore_116_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_116_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_116_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_116_ram_bypass)>(scalar);
        break;
    case 1359:
        ref->sigFromSrams_bore_116_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_116_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_116_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_116_ram_bp_clken)>(scalar);
        break;
    case 1360:
        ref->sigFromSrams_bore_116_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_116_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_116_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_116_ram_aux_clk)>(scalar);
        break;
    case 1361:
        ref->sigFromSrams_bore_116_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_116_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_116_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_116_ram_aux_ckbp)>(scalar);
        break;
    case 1362:
        ref->sigFromSrams_bore_116_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_116_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_116_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_116_ram_mcp_hold)>(scalar);
        break;
    case 1363:
        ref->sigFromSrams_bore_116_cgen = as_scalar<decltype(ref->sigFromSrams_bore_116_cgen)>(scalar);
        wolf->sigFromSrams_bore_116_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_116_cgen)>(scalar);
        break;
    case 1364:
        ref->sigFromSrams_bore_117_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_117_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_117_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_117_ram_hold)>(scalar);
        break;
    case 1365:
        ref->sigFromSrams_bore_117_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_117_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_117_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_117_ram_bypass)>(scalar);
        break;
    case 1366:
        ref->sigFromSrams_bore_117_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_117_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_117_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_117_ram_bp_clken)>(scalar);
        break;
    case 1367:
        ref->sigFromSrams_bore_117_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_117_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_117_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_117_ram_aux_clk)>(scalar);
        break;
    case 1368:
        ref->sigFromSrams_bore_117_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_117_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_117_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_117_ram_aux_ckbp)>(scalar);
        break;
    case 1369:
        ref->sigFromSrams_bore_117_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_117_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_117_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_117_ram_mcp_hold)>(scalar);
        break;
    case 1370:
        ref->sigFromSrams_bore_117_cgen = as_scalar<decltype(ref->sigFromSrams_bore_117_cgen)>(scalar);
        wolf->sigFromSrams_bore_117_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_117_cgen)>(scalar);
        break;
    case 1371:
        ref->sigFromSrams_bore_118_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_118_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_118_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_118_ram_hold)>(scalar);
        break;
    case 1372:
        ref->sigFromSrams_bore_118_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_118_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_118_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_118_ram_bypass)>(scalar);
        break;
    case 1373:
        ref->sigFromSrams_bore_118_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_118_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_118_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_118_ram_bp_clken)>(scalar);
        break;
    case 1374:
        ref->sigFromSrams_bore_118_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_118_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_118_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_118_ram_aux_clk)>(scalar);
        break;
    case 1375:
        ref->sigFromSrams_bore_118_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_118_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_118_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_118_ram_aux_ckbp)>(scalar);
        break;
    case 1376:
        ref->sigFromSrams_bore_118_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_118_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_118_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_118_ram_mcp_hold)>(scalar);
        break;
    case 1377:
        ref->sigFromSrams_bore_118_cgen = as_scalar<decltype(ref->sigFromSrams_bore_118_cgen)>(scalar);
        wolf->sigFromSrams_bore_118_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_118_cgen)>(scalar);
        break;
    case 1378:
        ref->sigFromSrams_bore_119_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_119_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_119_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_119_ram_hold)>(scalar);
        break;
    case 1379:
        ref->sigFromSrams_bore_119_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_119_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_119_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_119_ram_bypass)>(scalar);
        break;
    case 1380:
        ref->sigFromSrams_bore_119_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_119_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_119_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_119_ram_bp_clken)>(scalar);
        break;
    case 1381:
        ref->sigFromSrams_bore_119_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_119_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_119_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_119_ram_aux_clk)>(scalar);
        break;
    case 1382:
        ref->sigFromSrams_bore_119_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_119_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_119_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_119_ram_aux_ckbp)>(scalar);
        break;
    case 1383:
        ref->sigFromSrams_bore_119_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_119_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_119_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_119_ram_mcp_hold)>(scalar);
        break;
    case 1384:
        ref->sigFromSrams_bore_119_cgen = as_scalar<decltype(ref->sigFromSrams_bore_119_cgen)>(scalar);
        wolf->sigFromSrams_bore_119_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_119_cgen)>(scalar);
        break;
    case 1385:
        ref->sigFromSrams_bore_120_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_120_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_120_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_120_ram_hold)>(scalar);
        break;
    case 1386:
        ref->sigFromSrams_bore_120_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_120_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_120_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_120_ram_bypass)>(scalar);
        break;
    case 1387:
        ref->sigFromSrams_bore_120_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_120_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_120_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_120_ram_bp_clken)>(scalar);
        break;
    case 1388:
        ref->sigFromSrams_bore_120_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_120_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_120_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_120_ram_aux_clk)>(scalar);
        break;
    case 1389:
        ref->sigFromSrams_bore_120_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_120_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_120_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_120_ram_aux_ckbp)>(scalar);
        break;
    case 1390:
        ref->sigFromSrams_bore_120_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_120_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_120_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_120_ram_mcp_hold)>(scalar);
        break;
    case 1391:
        ref->sigFromSrams_bore_120_cgen = as_scalar<decltype(ref->sigFromSrams_bore_120_cgen)>(scalar);
        wolf->sigFromSrams_bore_120_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_120_cgen)>(scalar);
        break;
    case 1392:
        ref->sigFromSrams_bore_121_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_121_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_121_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_121_ram_hold)>(scalar);
        break;
    case 1393:
        ref->sigFromSrams_bore_121_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_121_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_121_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_121_ram_bypass)>(scalar);
        break;
    case 1394:
        ref->sigFromSrams_bore_121_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_121_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_121_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_121_ram_bp_clken)>(scalar);
        break;
    case 1395:
        ref->sigFromSrams_bore_121_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_121_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_121_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_121_ram_aux_clk)>(scalar);
        break;
    case 1396:
        ref->sigFromSrams_bore_121_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_121_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_121_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_121_ram_aux_ckbp)>(scalar);
        break;
    case 1397:
        ref->sigFromSrams_bore_121_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_121_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_121_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_121_ram_mcp_hold)>(scalar);
        break;
    case 1398:
        ref->sigFromSrams_bore_121_cgen = as_scalar<decltype(ref->sigFromSrams_bore_121_cgen)>(scalar);
        wolf->sigFromSrams_bore_121_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_121_cgen)>(scalar);
        break;
    case 1399:
        ref->sigFromSrams_bore_122_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_122_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_122_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_122_ram_hold)>(scalar);
        break;
    case 1400:
        ref->sigFromSrams_bore_122_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_122_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_122_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_122_ram_bypass)>(scalar);
        break;
    case 1401:
        ref->sigFromSrams_bore_122_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_122_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_122_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_122_ram_bp_clken)>(scalar);
        break;
    case 1402:
        ref->sigFromSrams_bore_122_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_122_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_122_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_122_ram_aux_clk)>(scalar);
        break;
    case 1403:
        ref->sigFromSrams_bore_122_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_122_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_122_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_122_ram_aux_ckbp)>(scalar);
        break;
    case 1404:
        ref->sigFromSrams_bore_122_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_122_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_122_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_122_ram_mcp_hold)>(scalar);
        break;
    case 1405:
        ref->sigFromSrams_bore_122_cgen = as_scalar<decltype(ref->sigFromSrams_bore_122_cgen)>(scalar);
        wolf->sigFromSrams_bore_122_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_122_cgen)>(scalar);
        break;
    case 1406:
        ref->sigFromSrams_bore_123_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_123_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_123_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_123_ram_hold)>(scalar);
        break;
    case 1407:
        ref->sigFromSrams_bore_123_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_123_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_123_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_123_ram_bypass)>(scalar);
        break;
    case 1408:
        ref->sigFromSrams_bore_123_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_123_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_123_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_123_ram_bp_clken)>(scalar);
        break;
    case 1409:
        ref->sigFromSrams_bore_123_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_123_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_123_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_123_ram_aux_clk)>(scalar);
        break;
    case 1410:
        ref->sigFromSrams_bore_123_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_123_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_123_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_123_ram_aux_ckbp)>(scalar);
        break;
    case 1411:
        ref->sigFromSrams_bore_123_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_123_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_123_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_123_ram_mcp_hold)>(scalar);
        break;
    case 1412:
        ref->sigFromSrams_bore_123_cgen = as_scalar<decltype(ref->sigFromSrams_bore_123_cgen)>(scalar);
        wolf->sigFromSrams_bore_123_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_123_cgen)>(scalar);
        break;
    case 1413:
        ref->sigFromSrams_bore_124_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_124_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_124_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_124_ram_hold)>(scalar);
        break;
    case 1414:
        ref->sigFromSrams_bore_124_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_124_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_124_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_124_ram_bypass)>(scalar);
        break;
    case 1415:
        ref->sigFromSrams_bore_124_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_124_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_124_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_124_ram_bp_clken)>(scalar);
        break;
    case 1416:
        ref->sigFromSrams_bore_124_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_124_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_124_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_124_ram_aux_clk)>(scalar);
        break;
    case 1417:
        ref->sigFromSrams_bore_124_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_124_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_124_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_124_ram_aux_ckbp)>(scalar);
        break;
    case 1418:
        ref->sigFromSrams_bore_124_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_124_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_124_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_124_ram_mcp_hold)>(scalar);
        break;
    case 1419:
        ref->sigFromSrams_bore_124_cgen = as_scalar<decltype(ref->sigFromSrams_bore_124_cgen)>(scalar);
        wolf->sigFromSrams_bore_124_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_124_cgen)>(scalar);
        break;
    case 1420:
        ref->sigFromSrams_bore_125_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_125_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_125_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_125_ram_hold)>(scalar);
        break;
    case 1421:
        ref->sigFromSrams_bore_125_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_125_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_125_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_125_ram_bypass)>(scalar);
        break;
    case 1422:
        ref->sigFromSrams_bore_125_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_125_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_125_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_125_ram_bp_clken)>(scalar);
        break;
    case 1423:
        ref->sigFromSrams_bore_125_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_125_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_125_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_125_ram_aux_clk)>(scalar);
        break;
    case 1424:
        ref->sigFromSrams_bore_125_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_125_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_125_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_125_ram_aux_ckbp)>(scalar);
        break;
    case 1425:
        ref->sigFromSrams_bore_125_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_125_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_125_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_125_ram_mcp_hold)>(scalar);
        break;
    case 1426:
        ref->sigFromSrams_bore_125_cgen = as_scalar<decltype(ref->sigFromSrams_bore_125_cgen)>(scalar);
        wolf->sigFromSrams_bore_125_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_125_cgen)>(scalar);
        break;
    case 1427:
        ref->sigFromSrams_bore_126_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_126_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_126_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_126_ram_hold)>(scalar);
        break;
    case 1428:
        ref->sigFromSrams_bore_126_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_126_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_126_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_126_ram_bypass)>(scalar);
        break;
    case 1429:
        ref->sigFromSrams_bore_126_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_126_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_126_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_126_ram_bp_clken)>(scalar);
        break;
    case 1430:
        ref->sigFromSrams_bore_126_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_126_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_126_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_126_ram_aux_clk)>(scalar);
        break;
    case 1431:
        ref->sigFromSrams_bore_126_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_126_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_126_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_126_ram_aux_ckbp)>(scalar);
        break;
    case 1432:
        ref->sigFromSrams_bore_126_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_126_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_126_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_126_ram_mcp_hold)>(scalar);
        break;
    case 1433:
        ref->sigFromSrams_bore_126_cgen = as_scalar<decltype(ref->sigFromSrams_bore_126_cgen)>(scalar);
        wolf->sigFromSrams_bore_126_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_126_cgen)>(scalar);
        break;
    case 1434:
        ref->sigFromSrams_bore_127_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_127_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_127_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_127_ram_hold)>(scalar);
        break;
    case 1435:
        ref->sigFromSrams_bore_127_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_127_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_127_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_127_ram_bypass)>(scalar);
        break;
    case 1436:
        ref->sigFromSrams_bore_127_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_127_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_127_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_127_ram_bp_clken)>(scalar);
        break;
    case 1437:
        ref->sigFromSrams_bore_127_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_127_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_127_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_127_ram_aux_clk)>(scalar);
        break;
    case 1438:
        ref->sigFromSrams_bore_127_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_127_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_127_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_127_ram_aux_ckbp)>(scalar);
        break;
    case 1439:
        ref->sigFromSrams_bore_127_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_127_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_127_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_127_ram_mcp_hold)>(scalar);
        break;
    case 1440:
        ref->sigFromSrams_bore_127_cgen = as_scalar<decltype(ref->sigFromSrams_bore_127_cgen)>(scalar);
        wolf->sigFromSrams_bore_127_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_127_cgen)>(scalar);
        break;
    case 1441:
        ref->sigFromSrams_bore_128_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_128_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_128_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_128_ram_hold)>(scalar);
        break;
    case 1442:
        ref->sigFromSrams_bore_128_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_128_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_128_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_128_ram_bypass)>(scalar);
        break;
    case 1443:
        ref->sigFromSrams_bore_128_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_128_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_128_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_128_ram_bp_clken)>(scalar);
        break;
    case 1444:
        ref->sigFromSrams_bore_128_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_128_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_128_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_128_ram_aux_clk)>(scalar);
        break;
    case 1445:
        ref->sigFromSrams_bore_128_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_128_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_128_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_128_ram_aux_ckbp)>(scalar);
        break;
    case 1446:
        ref->sigFromSrams_bore_128_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_128_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_128_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_128_ram_mcp_hold)>(scalar);
        break;
    case 1447:
        ref->sigFromSrams_bore_128_cgen = as_scalar<decltype(ref->sigFromSrams_bore_128_cgen)>(scalar);
        wolf->sigFromSrams_bore_128_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_128_cgen)>(scalar);
        break;
    case 1448:
        ref->sigFromSrams_bore_129_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_129_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_129_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_129_ram_hold)>(scalar);
        break;
    case 1449:
        ref->sigFromSrams_bore_129_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_129_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_129_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_129_ram_bypass)>(scalar);
        break;
    case 1450:
        ref->sigFromSrams_bore_129_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_129_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_129_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_129_ram_bp_clken)>(scalar);
        break;
    case 1451:
        ref->sigFromSrams_bore_129_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_129_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_129_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_129_ram_aux_clk)>(scalar);
        break;
    case 1452:
        ref->sigFromSrams_bore_129_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_129_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_129_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_129_ram_aux_ckbp)>(scalar);
        break;
    case 1453:
        ref->sigFromSrams_bore_129_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_129_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_129_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_129_ram_mcp_hold)>(scalar);
        break;
    case 1454:
        ref->sigFromSrams_bore_129_cgen = as_scalar<decltype(ref->sigFromSrams_bore_129_cgen)>(scalar);
        wolf->sigFromSrams_bore_129_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_129_cgen)>(scalar);
        break;
    case 1455:
        ref->sigFromSrams_bore_130_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_130_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_130_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_130_ram_hold)>(scalar);
        break;
    case 1456:
        ref->sigFromSrams_bore_130_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_130_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_130_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_130_ram_bypass)>(scalar);
        break;
    case 1457:
        ref->sigFromSrams_bore_130_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_130_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_130_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_130_ram_bp_clken)>(scalar);
        break;
    case 1458:
        ref->sigFromSrams_bore_130_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_130_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_130_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_130_ram_aux_clk)>(scalar);
        break;
    case 1459:
        ref->sigFromSrams_bore_130_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_130_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_130_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_130_ram_aux_ckbp)>(scalar);
        break;
    case 1460:
        ref->sigFromSrams_bore_130_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_130_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_130_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_130_ram_mcp_hold)>(scalar);
        break;
    case 1461:
        ref->sigFromSrams_bore_130_cgen = as_scalar<decltype(ref->sigFromSrams_bore_130_cgen)>(scalar);
        wolf->sigFromSrams_bore_130_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_130_cgen)>(scalar);
        break;
    case 1462:
        ref->sigFromSrams_bore_131_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_131_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_131_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_131_ram_hold)>(scalar);
        break;
    case 1463:
        ref->sigFromSrams_bore_131_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_131_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_131_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_131_ram_bypass)>(scalar);
        break;
    case 1464:
        ref->sigFromSrams_bore_131_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_131_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_131_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_131_ram_bp_clken)>(scalar);
        break;
    case 1465:
        ref->sigFromSrams_bore_131_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_131_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_131_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_131_ram_aux_clk)>(scalar);
        break;
    case 1466:
        ref->sigFromSrams_bore_131_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_131_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_131_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_131_ram_aux_ckbp)>(scalar);
        break;
    case 1467:
        ref->sigFromSrams_bore_131_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_131_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_131_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_131_ram_mcp_hold)>(scalar);
        break;
    case 1468:
        ref->sigFromSrams_bore_131_cgen = as_scalar<decltype(ref->sigFromSrams_bore_131_cgen)>(scalar);
        wolf->sigFromSrams_bore_131_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_131_cgen)>(scalar);
        break;
    case 1469:
        ref->sigFromSrams_bore_132_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_132_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_132_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_132_ram_hold)>(scalar);
        break;
    case 1470:
        ref->sigFromSrams_bore_132_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_132_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_132_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_132_ram_bypass)>(scalar);
        break;
    case 1471:
        ref->sigFromSrams_bore_132_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_132_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_132_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_132_ram_bp_clken)>(scalar);
        break;
    case 1472:
        ref->sigFromSrams_bore_132_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_132_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_132_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_132_ram_aux_clk)>(scalar);
        break;
    case 1473:
        ref->sigFromSrams_bore_132_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_132_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_132_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_132_ram_aux_ckbp)>(scalar);
        break;
    case 1474:
        ref->sigFromSrams_bore_132_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_132_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_132_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_132_ram_mcp_hold)>(scalar);
        break;
    case 1475:
        ref->sigFromSrams_bore_132_cgen = as_scalar<decltype(ref->sigFromSrams_bore_132_cgen)>(scalar);
        wolf->sigFromSrams_bore_132_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_132_cgen)>(scalar);
        break;
    case 1476:
        ref->sigFromSrams_bore_133_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_133_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_133_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_133_ram_hold)>(scalar);
        break;
    case 1477:
        ref->sigFromSrams_bore_133_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_133_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_133_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_133_ram_bypass)>(scalar);
        break;
    case 1478:
        ref->sigFromSrams_bore_133_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_133_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_133_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_133_ram_bp_clken)>(scalar);
        break;
    case 1479:
        ref->sigFromSrams_bore_133_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_133_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_133_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_133_ram_aux_clk)>(scalar);
        break;
    case 1480:
        ref->sigFromSrams_bore_133_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_133_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_133_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_133_ram_aux_ckbp)>(scalar);
        break;
    case 1481:
        ref->sigFromSrams_bore_133_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_133_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_133_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_133_ram_mcp_hold)>(scalar);
        break;
    case 1482:
        ref->sigFromSrams_bore_133_cgen = as_scalar<decltype(ref->sigFromSrams_bore_133_cgen)>(scalar);
        wolf->sigFromSrams_bore_133_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_133_cgen)>(scalar);
        break;
    case 1483:
        ref->sigFromSrams_bore_134_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_134_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_134_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_134_ram_hold)>(scalar);
        break;
    case 1484:
        ref->sigFromSrams_bore_134_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_134_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_134_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_134_ram_bypass)>(scalar);
        break;
    case 1485:
        ref->sigFromSrams_bore_134_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_134_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_134_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_134_ram_bp_clken)>(scalar);
        break;
    case 1486:
        ref->sigFromSrams_bore_134_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_134_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_134_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_134_ram_aux_clk)>(scalar);
        break;
    case 1487:
        ref->sigFromSrams_bore_134_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_134_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_134_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_134_ram_aux_ckbp)>(scalar);
        break;
    case 1488:
        ref->sigFromSrams_bore_134_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_134_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_134_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_134_ram_mcp_hold)>(scalar);
        break;
    case 1489:
        ref->sigFromSrams_bore_134_cgen = as_scalar<decltype(ref->sigFromSrams_bore_134_cgen)>(scalar);
        wolf->sigFromSrams_bore_134_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_134_cgen)>(scalar);
        break;
    case 1490:
        ref->sigFromSrams_bore_135_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_135_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_135_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_135_ram_hold)>(scalar);
        break;
    case 1491:
        ref->sigFromSrams_bore_135_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_135_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_135_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_135_ram_bypass)>(scalar);
        break;
    case 1492:
        ref->sigFromSrams_bore_135_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_135_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_135_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_135_ram_bp_clken)>(scalar);
        break;
    case 1493:
        ref->sigFromSrams_bore_135_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_135_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_135_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_135_ram_aux_clk)>(scalar);
        break;
    case 1494:
        ref->sigFromSrams_bore_135_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_135_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_135_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_135_ram_aux_ckbp)>(scalar);
        break;
    case 1495:
        ref->sigFromSrams_bore_135_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_135_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_135_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_135_ram_mcp_hold)>(scalar);
        break;
    case 1496:
        ref->sigFromSrams_bore_135_cgen = as_scalar<decltype(ref->sigFromSrams_bore_135_cgen)>(scalar);
        wolf->sigFromSrams_bore_135_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_135_cgen)>(scalar);
        break;
    case 1497:
        ref->sigFromSrams_bore_136_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_136_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_136_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_136_ram_hold)>(scalar);
        break;
    case 1498:
        ref->sigFromSrams_bore_136_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_136_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_136_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_136_ram_bypass)>(scalar);
        break;
    case 1499:
        ref->sigFromSrams_bore_136_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_136_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_136_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_136_ram_bp_clken)>(scalar);
        break;
    case 1500:
        ref->sigFromSrams_bore_136_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_136_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_136_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_136_ram_aux_clk)>(scalar);
        break;
    case 1501:
        ref->sigFromSrams_bore_136_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_136_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_136_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_136_ram_aux_ckbp)>(scalar);
        break;
    case 1502:
        ref->sigFromSrams_bore_136_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_136_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_136_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_136_ram_mcp_hold)>(scalar);
        break;
    case 1503:
        ref->sigFromSrams_bore_136_cgen = as_scalar<decltype(ref->sigFromSrams_bore_136_cgen)>(scalar);
        wolf->sigFromSrams_bore_136_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_136_cgen)>(scalar);
        break;
    case 1504:
        ref->sigFromSrams_bore_137_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_137_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_137_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_137_ram_hold)>(scalar);
        break;
    case 1505:
        ref->sigFromSrams_bore_137_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_137_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_137_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_137_ram_bypass)>(scalar);
        break;
    case 1506:
        ref->sigFromSrams_bore_137_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_137_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_137_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_137_ram_bp_clken)>(scalar);
        break;
    case 1507:
        ref->sigFromSrams_bore_137_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_137_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_137_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_137_ram_aux_clk)>(scalar);
        break;
    case 1508:
        ref->sigFromSrams_bore_137_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_137_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_137_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_137_ram_aux_ckbp)>(scalar);
        break;
    case 1509:
        ref->sigFromSrams_bore_137_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_137_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_137_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_137_ram_mcp_hold)>(scalar);
        break;
    case 1510:
        ref->sigFromSrams_bore_137_cgen = as_scalar<decltype(ref->sigFromSrams_bore_137_cgen)>(scalar);
        wolf->sigFromSrams_bore_137_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_137_cgen)>(scalar);
        break;
    case 1511:
        ref->sigFromSrams_bore_138_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_138_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_138_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_138_ram_hold)>(scalar);
        break;
    case 1512:
        ref->sigFromSrams_bore_138_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_138_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_138_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_138_ram_bypass)>(scalar);
        break;
    case 1513:
        ref->sigFromSrams_bore_138_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_138_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_138_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_138_ram_bp_clken)>(scalar);
        break;
    case 1514:
        ref->sigFromSrams_bore_138_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_138_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_138_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_138_ram_aux_clk)>(scalar);
        break;
    case 1515:
        ref->sigFromSrams_bore_138_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_138_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_138_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_138_ram_aux_ckbp)>(scalar);
        break;
    case 1516:
        ref->sigFromSrams_bore_138_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_138_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_138_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_138_ram_mcp_hold)>(scalar);
        break;
    case 1517:
        ref->sigFromSrams_bore_138_cgen = as_scalar<decltype(ref->sigFromSrams_bore_138_cgen)>(scalar);
        wolf->sigFromSrams_bore_138_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_138_cgen)>(scalar);
        break;
    case 1518:
        ref->sigFromSrams_bore_139_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_139_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_139_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_139_ram_hold)>(scalar);
        break;
    case 1519:
        ref->sigFromSrams_bore_139_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_139_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_139_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_139_ram_bypass)>(scalar);
        break;
    case 1520:
        ref->sigFromSrams_bore_139_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_139_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_139_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_139_ram_bp_clken)>(scalar);
        break;
    case 1521:
        ref->sigFromSrams_bore_139_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_139_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_139_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_139_ram_aux_clk)>(scalar);
        break;
    case 1522:
        ref->sigFromSrams_bore_139_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_139_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_139_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_139_ram_aux_ckbp)>(scalar);
        break;
    case 1523:
        ref->sigFromSrams_bore_139_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_139_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_139_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_139_ram_mcp_hold)>(scalar);
        break;
    case 1524:
        ref->sigFromSrams_bore_139_cgen = as_scalar<decltype(ref->sigFromSrams_bore_139_cgen)>(scalar);
        wolf->sigFromSrams_bore_139_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_139_cgen)>(scalar);
        break;
    case 1525:
        ref->sigFromSrams_bore_140_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_140_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_140_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_140_ram_hold)>(scalar);
        break;
    case 1526:
        ref->sigFromSrams_bore_140_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_140_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_140_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_140_ram_bypass)>(scalar);
        break;
    case 1527:
        ref->sigFromSrams_bore_140_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_140_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_140_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_140_ram_bp_clken)>(scalar);
        break;
    case 1528:
        ref->sigFromSrams_bore_140_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_140_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_140_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_140_ram_aux_clk)>(scalar);
        break;
    case 1529:
        ref->sigFromSrams_bore_140_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_140_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_140_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_140_ram_aux_ckbp)>(scalar);
        break;
    case 1530:
        ref->sigFromSrams_bore_140_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_140_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_140_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_140_ram_mcp_hold)>(scalar);
        break;
    case 1531:
        ref->sigFromSrams_bore_140_cgen = as_scalar<decltype(ref->sigFromSrams_bore_140_cgen)>(scalar);
        wolf->sigFromSrams_bore_140_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_140_cgen)>(scalar);
        break;
    case 1532:
        ref->sigFromSrams_bore_141_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_141_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_141_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_141_ram_hold)>(scalar);
        break;
    case 1533:
        ref->sigFromSrams_bore_141_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_141_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_141_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_141_ram_bypass)>(scalar);
        break;
    case 1534:
        ref->sigFromSrams_bore_141_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_141_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_141_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_141_ram_bp_clken)>(scalar);
        break;
    case 1535:
        ref->sigFromSrams_bore_141_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_141_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_141_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_141_ram_aux_clk)>(scalar);
        break;
    case 1536:
        ref->sigFromSrams_bore_141_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_141_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_141_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_141_ram_aux_ckbp)>(scalar);
        break;
    case 1537:
        ref->sigFromSrams_bore_141_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_141_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_141_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_141_ram_mcp_hold)>(scalar);
        break;
    case 1538:
        ref->sigFromSrams_bore_141_cgen = as_scalar<decltype(ref->sigFromSrams_bore_141_cgen)>(scalar);
        wolf->sigFromSrams_bore_141_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_141_cgen)>(scalar);
        break;
    case 1539:
        ref->sigFromSrams_bore_142_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_142_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_142_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_142_ram_hold)>(scalar);
        break;
    case 1540:
        ref->sigFromSrams_bore_142_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_142_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_142_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_142_ram_bypass)>(scalar);
        break;
    case 1541:
        ref->sigFromSrams_bore_142_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_142_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_142_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_142_ram_bp_clken)>(scalar);
        break;
    case 1542:
        ref->sigFromSrams_bore_142_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_142_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_142_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_142_ram_aux_clk)>(scalar);
        break;
    case 1543:
        ref->sigFromSrams_bore_142_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_142_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_142_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_142_ram_aux_ckbp)>(scalar);
        break;
    case 1544:
        ref->sigFromSrams_bore_142_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_142_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_142_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_142_ram_mcp_hold)>(scalar);
        break;
    case 1545:
        ref->sigFromSrams_bore_142_cgen = as_scalar<decltype(ref->sigFromSrams_bore_142_cgen)>(scalar);
        wolf->sigFromSrams_bore_142_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_142_cgen)>(scalar);
        break;
    case 1546:
        ref->sigFromSrams_bore_143_ram_hold = as_scalar<decltype(ref->sigFromSrams_bore_143_ram_hold)>(scalar);
        wolf->sigFromSrams_bore_143_ram_hold = as_scalar<decltype(wolf->sigFromSrams_bore_143_ram_hold)>(scalar);
        break;
    case 1547:
        ref->sigFromSrams_bore_143_ram_bypass = as_scalar<decltype(ref->sigFromSrams_bore_143_ram_bypass)>(scalar);
        wolf->sigFromSrams_bore_143_ram_bypass = as_scalar<decltype(wolf->sigFromSrams_bore_143_ram_bypass)>(scalar);
        break;
    case 1548:
        ref->sigFromSrams_bore_143_ram_bp_clken = as_scalar<decltype(ref->sigFromSrams_bore_143_ram_bp_clken)>(scalar);
        wolf->sigFromSrams_bore_143_ram_bp_clken = as_scalar<decltype(wolf->sigFromSrams_bore_143_ram_bp_clken)>(scalar);
        break;
    case 1549:
        ref->sigFromSrams_bore_143_ram_aux_clk = as_scalar<decltype(ref->sigFromSrams_bore_143_ram_aux_clk)>(scalar);
        wolf->sigFromSrams_bore_143_ram_aux_clk = as_scalar<decltype(wolf->sigFromSrams_bore_143_ram_aux_clk)>(scalar);
        break;
    case 1550:
        ref->sigFromSrams_bore_143_ram_aux_ckbp = as_scalar<decltype(ref->sigFromSrams_bore_143_ram_aux_ckbp)>(scalar);
        wolf->sigFromSrams_bore_143_ram_aux_ckbp = as_scalar<decltype(wolf->sigFromSrams_bore_143_ram_aux_ckbp)>(scalar);
        break;
    case 1551:
        ref->sigFromSrams_bore_143_ram_mcp_hold = as_scalar<decltype(ref->sigFromSrams_bore_143_ram_mcp_hold)>(scalar);
        wolf->sigFromSrams_bore_143_ram_mcp_hold = as_scalar<decltype(wolf->sigFromSrams_bore_143_ram_mcp_hold)>(scalar);
        break;
    case 1552:
        ref->sigFromSrams_bore_143_cgen = as_scalar<decltype(ref->sigFromSrams_bore_143_cgen)>(scalar);
        wolf->sigFromSrams_bore_143_cgen = as_scalar<decltype(wolf->sigFromSrams_bore_143_cgen)>(scalar);
        break;
    default:
        break;
    }
}

static void write_coverage() {
    const char *cov = std::getenv("VERILATOR_COV_FILE");
    if (cov && cov[0]) {
        VerilatedCov::write(cov);
    }
}

static int compare_step(const VRef *ref, const VWolf *wolf, int t) {
    if (ref->io_toFtq_prediction_ready_o != wolf->io_toFtq_prediction_ready_o) {
        std::fprintf(stderr, "[MISMATCH] t=%d io_toFtq_prediction_ready ref=%u wolf=%u\n",
                     t, ref->io_toFtq_prediction_ready_o, wolf->io_toFtq_prediction_ready_o);
        return 1;
    }
    if (ref->s1_fire_o != wolf->s1_fire_o) {
        std::fprintf(stderr, "[MISMATCH] t=%d s1_fire ref=%u wolf=%u\n",
                     t, ref->s1_fire_o, wolf->s1_fire_o);
        return 1;
    }
    if (ref->abtb_io_stageCtrl_s0_fire_probe_o != wolf->abtb_io_stageCtrl_s0_fire_probe_o) {
        std::fprintf(stderr,
                     "[MISMATCH] t=%d abtb_io_stageCtrl_s0_fire_probe ref=%u wolf=%u\n",
                     t, ref->abtb_io_stageCtrl_s0_fire_probe_o,
                     wolf->abtb_io_stageCtrl_s0_fire_probe_o);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::randReset(0);
    Verilated::randSeed(1);

    const char *events_path = std::getenv("EVENTS_CSV");
    std::ifstream fin(events_path ? events_path : "events.csv");
    if (!fin.is_open()) {
        std::fprintf(stderr, "Failed to open events.csv\n");
        return 1;
    }
    std::string line;
    if (!std::getline(fin, line)) {
        std::fprintf(stderr, "events.csv empty\n");
        return 1;
    }

    Event event;
    bool has_event = false;
    if (std::getline(fin, line)) {
        has_event = parse_event_line(line, event);
    }
    if (!has_event) {
        std::fprintf(stderr, "No events found\n");
        return 1;
    }

    const int t0 = event.time;
    int t1 = event.time;
    std::vector<Event> buffered;
    buffered.reserve(4);

    // Scan to find last time while buffering events (small file).
    buffered.push_back(event);
    while (std::getline(fin, line)) {
        Event ev;
        if (!parse_event_line(line, ev)) {
            continue;
        }
        buffered.push_back(ev);
        if (ev.time > t1) {
            t1 = ev.time;
        }
    }
    fin.close();

    VRef *ref = new VRef;
    VWolf *wolf = new VWolf;

    std::vector<uint32_t> words;
    uint64_t scalar = 0;
    size_t idx = 0;

    for (int t = t0; t <= t1; ++t) {
        main_time = static_cast<vluint64_t>(t);
        while (idx < buffered.size() && buffered[idx].time == t) {
            const int sig_id = buffered[idx].signal_id;
            if (sig_id < 0 || sig_id >= static_cast<int>(sizeof(kSignals) / sizeof(kSignals[0]))) {
                ++idx;
                continue;
            }
            parse_value(buffered[idx].value, kSignals[sig_id].width, words, scalar);
            apply_signal(ref, wolf, sig_id, words, scalar);
            ++idx;
        }
        ref->eval();
        wolf->eval();
        if (compare_step(ref, wolf, t) != 0) {
            write_coverage();
            delete ref;
            delete wolf;
            return 1;
        }
    }

    write_coverage();
    delete ref;
    delete wolf;
    return 0;
}
