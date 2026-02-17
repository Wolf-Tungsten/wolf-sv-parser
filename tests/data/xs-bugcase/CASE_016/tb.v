`timescale 1ns/1ps

module xs_bugcase_tb (
    input  logic clk,
    input  logic rst_n,
    output logic io_fromBpu_prediction_ready_o
);

    logic reset;
    assign reset = ~rst_n;

    // clk and reset are ports
    
    // Output from DUT
    logic io_fromBpu_prediction_ready;
    
    // Inputs to DUT (to be driven by trace replay)
    logic io_fromBpu_prediction_valid;
    logic [48:0] io_fromBpu_prediction_bits_startPc_addr;
    logic [48:0] io_fromBpu_prediction_bits_target_addr;
    logic io_fromBpu_prediction_bits_takenCfiOffset_valid;
    logic [4:0] io_fromBpu_prediction_bits_takenCfiOffset_bits;
    logic io_fromBpu_prediction_bits_s3Override;
    logic io_fromBpu_meta_valid;
    logic io_fromBpu_meta_bits_redirectMeta_phr_phrPtr_flag;
    logic [9:0] io_fromBpu_meta_bits_redirectMeta_phr_phrPtr_value;
    logic [12:0] io_fromBpu_meta_bits_redirectMeta_phr_phrLowBits;
    logic [15:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_ghr;
    logic [7:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_bw;
    logic io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_0;
    logic io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_1;
    logic io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_2;
    logic io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_3;
    logic io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_4;
    logic io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_5;
    logic io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_6;
    logic io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_7;
    logic [1:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_0_branchType;
    logic [1:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_1_branchType;
    logic [1:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_2_branchType;
    logic [1:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_3_branchType;
    logic [1:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_4_branchType;
    logic [1:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_5_branchType;
    logic [1:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_6_branchType;
    logic [1:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_7_branchType;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_0;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_1;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_2;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_3;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_4;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_5;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_6;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_7;
    logic [3:0] io_fromBpu_meta_bits_redirectMeta_ras_ssp;
    logic [2:0] io_fromBpu_meta_bits_redirectMeta_ras_sctr;
    logic io_fromBpu_meta_bits_redirectMeta_ras_tosw_flag;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_ras_tosw_value;
    logic io_fromBpu_meta_bits_redirectMeta_ras_tosr_flag;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_ras_tosr_value;
    logic io_fromBpu_meta_bits_redirectMeta_ras_nos_flag;
    logic [4:0] io_fromBpu_meta_bits_redirectMeta_ras_nos_value;
    logic [48:0] io_fromBpu_meta_bits_redirectMeta_ras_topRetAddr_addr;
    logic io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_rawHit;
    logic [4:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_position;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_branchType;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_rasAction;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_counter_value;
    logic io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_rawHit;
    logic [4:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_position;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_branchType;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_rasAction;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_counter_value;
    logic io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_rawHit;
    logic [4:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_position;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_branchType;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_rasAction;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_counter_value;
    logic io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_rawHit;
    logic [4:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_position;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_branchType;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_rasAction;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_counter_value;
    logic io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_rawHit;
    logic [4:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_position;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_branchType;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_rasAction;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_counter_value;
    logic io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_rawHit;
    logic [4:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_position;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_branchType;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_rasAction;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_counter_value;
    logic io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_rawHit;
    logic [4:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_position;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_branchType;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_rasAction;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_counter_value;
    logic io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_rawHit;
    logic [4:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_position;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_branchType;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_rasAction;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_counter_value;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_0_useProvider;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerTableIdx;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerWayIdx;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerTakenCtr_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerUsefulCtr_value;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_0_altOrBasePred;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_1_useProvider;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerTableIdx;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerWayIdx;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerTakenCtr_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerUsefulCtr_value;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_1_altOrBasePred;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_2_useProvider;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerTableIdx;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerWayIdx;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerTakenCtr_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerUsefulCtr_value;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_2_altOrBasePred;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_3_useProvider;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerTableIdx;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerWayIdx;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerTakenCtr_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerUsefulCtr_value;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_3_altOrBasePred;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_4_useProvider;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerTableIdx;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerWayIdx;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerTakenCtr_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerUsefulCtr_value;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_4_altOrBasePred;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_5_useProvider;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerTableIdx;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerWayIdx;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerTakenCtr_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerUsefulCtr_value;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_5_altOrBasePred;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_6_useProvider;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerTableIdx;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerWayIdx;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerTakenCtr_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerUsefulCtr_value;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_6_altOrBasePred;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_7_useProvider;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerTableIdx;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerWayIdx;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerTakenCtr_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerUsefulCtr_value;
    logic io_fromBpu_meta_bits_resolveMeta_tage_entries_7_altOrBasePred;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_0;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_1;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_2;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_3;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_4;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_5;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_6;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_7;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_0;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_1;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_2;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_3;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_4;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_5;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_6;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_7;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_0;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_1;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_2;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_3;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_4;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_5;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_6;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_7;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_8;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_9;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_10;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_11;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_12;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_13;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_14;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_15;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_16;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_17;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_18;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_19;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_20;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_21;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_22;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_23;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_24;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_25;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_26;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_27;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_28;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_29;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_30;
    logic [5:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_31;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_0;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_1;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_2;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_3;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_4;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_5;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_6;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_7;
    logic io_fromBpu_meta_bits_resolveMeta_sc_scCommonHR_valid;
    logic [15:0] io_fromBpu_meta_bits_resolveMeta_sc_scCommonHR_ghr;
    logic [7:0] io_fromBpu_meta_bits_resolveMeta_sc_scCommonHR_bw;
    logic io_fromBpu_meta_bits_resolveMeta_sc_scPred_0;
    logic io_fromBpu_meta_bits_resolveMeta_sc_scPred_1;
    logic io_fromBpu_meta_bits_resolveMeta_sc_scPred_2;
    logic io_fromBpu_meta_bits_resolveMeta_sc_scPred_3;
    logic io_fromBpu_meta_bits_resolveMeta_sc_scPred_4;
    logic io_fromBpu_meta_bits_resolveMeta_sc_scPred_5;
    logic io_fromBpu_meta_bits_resolveMeta_sc_scPred_6;
    logic io_fromBpu_meta_bits_resolveMeta_sc_scPred_7;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePred_0;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePred_1;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePred_2;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePred_3;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePred_4;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePred_5;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePred_6;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePred_7;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_0;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_1;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_2;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_3;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_4;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_5;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_6;
    logic io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_7;
    logic io_fromBpu_meta_bits_resolveMeta_sc_useScPred_0;
    logic io_fromBpu_meta_bits_resolveMeta_sc_useScPred_1;
    logic io_fromBpu_meta_bits_resolveMeta_sc_useScPred_2;
    logic io_fromBpu_meta_bits_resolveMeta_sc_useScPred_3;
    logic io_fromBpu_meta_bits_resolveMeta_sc_useScPred_4;
    logic io_fromBpu_meta_bits_resolveMeta_sc_useScPred_5;
    logic io_fromBpu_meta_bits_resolveMeta_sc_useScPred_6;
    logic io_fromBpu_meta_bits_resolveMeta_sc_useScPred_7;
    logic io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_0;
    logic io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_1;
    logic io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_2;
    logic io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_3;
    logic io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_4;
    logic io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_5;
    logic io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_6;
    logic io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_7;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_0;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_1;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_2;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_3;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_4;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_5;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_6;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_7;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_0;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_1;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_2;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_3;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_4;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_5;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_6;
    logic io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_7;
    logic [6:0] io_fromBpu_meta_bits_resolveMeta_sc_debug_predPathIdx_0;
    logic [6:0] io_fromBpu_meta_bits_resolveMeta_sc_debug_predPathIdx_1;
    logic [6:0] io_fromBpu_meta_bits_resolveMeta_sc_debug_predGlobalIdx_0;
    logic [6:0] io_fromBpu_meta_bits_resolveMeta_sc_debug_predGlobalIdx_1;
    logic [6:0] io_fromBpu_meta_bits_resolveMeta_sc_debug_predBWIdx_0;
    logic [6:0] io_fromBpu_meta_bits_resolveMeta_sc_debug_predBWIdx_1;
    logic [6:0] io_fromBpu_meta_bits_resolveMeta_sc_debug_predBiasIdx;
    logic io_fromBpu_meta_bits_resolveMeta_ittage_provider_valid;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_ittage_provider_bits;
    logic io_fromBpu_meta_bits_resolveMeta_ittage_altProvider_valid;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_ittage_altProvider_bits;
    logic io_fromBpu_meta_bits_resolveMeta_ittage_altDiffers;
    logic io_fromBpu_meta_bits_resolveMeta_ittage_providerUsefulCnt_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_ittage_providerCnt_value;
    logic [1:0] io_fromBpu_meta_bits_resolveMeta_ittage_altProviderCnt_value;
    logic io_fromBpu_meta_bits_resolveMeta_ittage_allocate_valid;
    logic [2:0] io_fromBpu_meta_bits_resolveMeta_ittage_allocate_bits;
    logic [48:0] io_fromBpu_meta_bits_resolveMeta_ittage_providerTarget_addr;
    logic [48:0] io_fromBpu_meta_bits_resolveMeta_ittage_altProviderTarget_addr;
    logic [9:0] io_fromBpu_meta_bits_resolveMeta_phr_phrPtr_value;
    logic [12:0] io_fromBpu_meta_bits_resolveMeta_phr_phrLowBits;
    logic [12:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_31_foldedHist;
    logic [11:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_30_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_29_foldedHist;
    logic [12:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_28_foldedHist;
    logic [11:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_27_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_26_foldedHist;
    logic [12:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_25_foldedHist;
    logic [11:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_24_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_23_foldedHist;
    logic [12:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_22_foldedHist;
    logic [11:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_21_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_20_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_19_foldedHist;
    logic [7:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_18_foldedHist;
    logic [12:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_17_foldedHist;
    logic [11:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_16_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_15_foldedHist;
    logic [12:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_14_foldedHist;
    logic [11:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_13_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_12_foldedHist;
    logic [11:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_11_foldedHist;
    logic [10:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_10_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_9_foldedHist;
    logic [7:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_8_foldedHist;
    logic [6:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_7_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_6_foldedHist;
    logic [7:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_5_foldedHist;
    logic [8:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_4_foldedHist;
    logic [7:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_3_foldedHist;
    logic [7:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_2_foldedHist;
    logic [6:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_1_foldedHist;
    logic [3:0] io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_0_foldedHist;
    logic [3:0] io_fromBpu_meta_bits_commitMeta_ras_ssp;
    logic io_fromBpu_meta_bits_commitMeta_ras_tosw_flag;
    logic [4:0] io_fromBpu_meta_bits_commitMeta_ras_tosw_value;
    logic io_fromBpu_s3FtqPtr_flag;
    logic [5:0] io_fromBpu_s3FtqPtr_value;
    logic [4:0] io_fromBpu_perfMeta_s1Prediction_cfiPosition;
    logic [48:0] io_fromBpu_perfMeta_s1Prediction_target_addr;
    logic [1:0] io_fromBpu_perfMeta_s1Prediction_attribute_branchType;
    logic [1:0] io_fromBpu_perfMeta_s1Prediction_attribute_rasAction;
    logic io_fromBpu_perfMeta_s1Prediction_taken;
    logic [4:0] io_fromBpu_perfMeta_s3Prediction_cfiPosition;
    logic [48:0] io_fromBpu_perfMeta_s3Prediction_target_addr;
    logic [1:0] io_fromBpu_perfMeta_s3Prediction_attribute_branchType;
    logic [1:0] io_fromBpu_perfMeta_s3Prediction_attribute_rasAction;
    logic io_fromBpu_perfMeta_s3Prediction_taken;
    logic io_fromBpu_perfMeta_mbtbMeta_entries_0_0_rawHit;
    logic [4:0] io_fromBpu_perfMeta_mbtbMeta_entries_0_0_position;
    logic io_fromBpu_perfMeta_mbtbMeta_entries_0_1_rawHit;
    logic [4:0] io_fromBpu_perfMeta_mbtbMeta_entries_0_1_position;
    logic io_fromBpu_perfMeta_mbtbMeta_entries_0_2_rawHit;
    logic [4:0] io_fromBpu_perfMeta_mbtbMeta_entries_0_2_position;
    logic io_fromBpu_perfMeta_mbtbMeta_entries_0_3_rawHit;
    logic [4:0] io_fromBpu_perfMeta_mbtbMeta_entries_0_3_position;
    logic io_fromBpu_perfMeta_mbtbMeta_entries_1_0_rawHit;
    logic [4:0] io_fromBpu_perfMeta_mbtbMeta_entries_1_0_position;
    logic io_fromBpu_perfMeta_mbtbMeta_entries_1_1_rawHit;
    logic [4:0] io_fromBpu_perfMeta_mbtbMeta_entries_1_1_position;
    logic io_fromBpu_perfMeta_mbtbMeta_entries_1_2_rawHit;
    logic [4:0] io_fromBpu_perfMeta_mbtbMeta_entries_1_2_position;
    logic io_fromBpu_perfMeta_mbtbMeta_entries_1_3_rawHit;
    logic [4:0] io_fromBpu_perfMeta_mbtbMeta_entries_1_3_position;
    logic [2:0] io_fromBpu_perfMeta_bpSource_s1Source;
    logic [2:0] io_fromBpu_perfMeta_bpSource_s3Source;
    logic io_fromBpu_perfMeta_bpSource_s3Override;
    logic io_toBpu_train_ready;
    logic io_fromIfu_mmioCommitRead_valid;
    logic io_fromIfu_mmioCommitRead_mmioFtqPtr_flag;
    logic [5:0] io_fromIfu_mmioCommitRead_mmioFtqPtr_value;
    logic io_fromIfu_wbRedirect_valid;
    logic io_fromIfu_wbRedirect_bits_ftqIdx_flag;
    logic [5:0] io_fromIfu_wbRedirect_bits_ftqIdx_value;
    logic [49:0] io_fromIfu_wbRedirect_bits_pc;
    logic io_fromIfu_wbRedirect_bits_taken;
    logic [4:0] io_fromIfu_wbRedirect_bits_ftqOffset;
    logic io_fromIfu_wbRedirect_bits_isRVC;
    logic [1:0] io_fromIfu_wbRedirect_bits_attribute_branchType;
    logic [1:0] io_fromIfu_wbRedirect_bits_attribute_rasAction;
    logic [49:0] io_fromIfu_wbRedirect_bits_target;
    logic io_toIfu_req_ready;
    logic io_toICache_prefetchReq_ready;
    logic io_fromBackend_redirect_valid;
    logic io_fromBackend_redirect_bits_ftqIdx_flag;
    logic [5:0] io_fromBackend_redirect_bits_ftqIdx_value;
    logic [49:0] io_fromBackend_redirect_bits_pc;
    logic io_fromBackend_redirect_bits_taken;
    logic [4:0] io_fromBackend_redirect_bits_ftqOffset;
    logic io_fromBackend_redirect_bits_isRVC;
    logic [1:0] io_fromBackend_redirect_bits_attribute_branchType;
    logic [1:0] io_fromBackend_redirect_bits_attribute_rasAction;
    logic [49:0] io_fromBackend_redirect_bits_target;
    logic io_fromBackend_redirect_bits_level;
    logic io_fromBackend_redirect_bits_backendIGPF;
    logic io_fromBackend_redirect_bits_backendIPF;
    logic io_fromBackend_redirect_bits_backendIAF;
    logic io_fromBackend_redirect_bits_isMisPred;
    logic io_fromBackend_redirect_bits_debugIsCtrl;
    logic io_fromBackend_redirect_bits_debugIsMemVio;
    logic io_fromBackend_ftqIdxAhead_0_valid;
    logic [5:0] io_fromBackend_ftqIdxAhead_0_bits_value;
    logic io_fromBackend_resolve_0_valid;
    logic io_fromBackend_resolve_0_bits_ftqIdx_flag;
    logic [5:0] io_fromBackend_resolve_0_bits_ftqIdx_value;
    logic [4:0] io_fromBackend_resolve_0_bits_ftqOffset;
    logic [48:0] io_fromBackend_resolve_0_bits_pc_addr;
    logic [48:0] io_fromBackend_resolve_0_bits_target_addr;
    logic io_fromBackend_resolve_0_bits_taken;
    logic io_fromBackend_resolve_0_bits_mispredict;
    logic [1:0] io_fromBackend_resolve_0_bits_attribute_branchType;
    logic [1:0] io_fromBackend_resolve_0_bits_attribute_rasAction;
    logic io_fromBackend_resolve_1_valid;
    logic io_fromBackend_resolve_1_bits_ftqIdx_flag;
    logic [5:0] io_fromBackend_resolve_1_bits_ftqIdx_value;
    logic [4:0] io_fromBackend_resolve_1_bits_ftqOffset;
    logic [48:0] io_fromBackend_resolve_1_bits_pc_addr;
    logic [48:0] io_fromBackend_resolve_1_bits_target_addr;
    logic io_fromBackend_resolve_1_bits_taken;
    logic io_fromBackend_resolve_1_bits_mispredict;
    logic [1:0] io_fromBackend_resolve_1_bits_attribute_branchType;
    logic [1:0] io_fromBackend_resolve_1_bits_attribute_rasAction;
    logic io_fromBackend_resolve_2_valid;
    logic io_fromBackend_resolve_2_bits_ftqIdx_flag;
    logic [5:0] io_fromBackend_resolve_2_bits_ftqIdx_value;
    logic [4:0] io_fromBackend_resolve_2_bits_ftqOffset;
    logic [48:0] io_fromBackend_resolve_2_bits_pc_addr;
    logic [48:0] io_fromBackend_resolve_2_bits_target_addr;
    logic io_fromBackend_resolve_2_bits_taken;
    logic io_fromBackend_resolve_2_bits_mispredict;
    logic [1:0] io_fromBackend_resolve_2_bits_attribute_branchType;
    logic [1:0] io_fromBackend_resolve_2_bits_attribute_rasAction;
    logic io_fromBackend_commit_valid;
    logic io_fromBackend_commit_bits_flag;
    logic [5:0] io_fromBackend_commit_bits_value;
    logic io_fromBackend_callRetCommit_0_valid;
    logic [1:0] io_fromBackend_callRetCommit_0_bits_rasAction;
    logic [5:0] io_fromBackend_callRetCommit_0_bits_ftqPtr_value;
    logic io_fromBackend_callRetCommit_1_valid;
    logic [1:0] io_fromBackend_callRetCommit_1_bits_rasAction;
    logic [5:0] io_fromBackend_callRetCommit_1_bits_ftqPtr_value;
    logic io_fromBackend_callRetCommit_2_valid;
    logic [1:0] io_fromBackend_callRetCommit_2_bits_rasAction;
    logic [5:0] io_fromBackend_callRetCommit_2_bits_ftqPtr_value;
    logic io_fromBackend_callRetCommit_3_valid;
    logic [1:0] io_fromBackend_callRetCommit_3_bits_rasAction;
    logic [5:0] io_fromBackend_callRetCommit_3_bits_ftqPtr_value;
    logic io_fromBackend_callRetCommit_4_valid;
    logic [1:0] io_fromBackend_callRetCommit_4_bits_rasAction;
    logic [5:0] io_fromBackend_callRetCommit_4_bits_ftqPtr_value;
    logic io_fromBackend_callRetCommit_5_valid;
    logic [1:0] io_fromBackend_callRetCommit_5_bits_rasAction;
    logic [5:0] io_fromBackend_callRetCommit_5_bits_ftqPtr_value;
    logic io_fromBackend_callRetCommit_6_valid;
    logic [1:0] io_fromBackend_callRetCommit_6_bits_rasAction;
    logic [5:0] io_fromBackend_callRetCommit_6_bits_ftqPtr_value;
    logic io_fromBackend_callRetCommit_7_valid;
    logic [1:0] io_fromBackend_callRetCommit_7_bits_rasAction;
    logic [5:0] io_fromBackend_callRetCommit_7_bits_ftqPtr_value;
    logic io_toBpu_redirect_valid;
    logic [48:0] io_toBpu_redirect_bits_cfiPc_addr;
    logic [48:0] io_toBpu_redirect_bits_target_addr;
    logic io_toBpu_redirect_bits_taken;
    logic [1:0] io_toBpu_redirect_bits_attribute_branchType;
    logic [1:0] io_toBpu_redirect_bits_attribute_rasAction;
    logic io_toBpu_redirect_bits_meta_phr_phrPtr_flag;
    logic [9:0] io_toBpu_redirect_bits_meta_phr_phrPtr_value;
    logic [12:0] io_toBpu_redirect_bits_meta_phr_phrLowBits;
    logic [15:0] io_toBpu_redirect_bits_meta_commonHRMeta_ghr;
    logic [7:0] io_toBpu_redirect_bits_meta_commonHRMeta_bw;
    logic io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_0;
    logic io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_1;
    logic io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_2;
    logic io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_3;
    logic io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_4;
    logic io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_5;
    logic io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_6;
    logic io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_7;
    logic [1:0] io_toBpu_redirect_bits_meta_commonHRMeta_attribute_0_branchType;
    logic [1:0] io_toBpu_redirect_bits_meta_commonHRMeta_attribute_1_branchType;
    logic [1:0] io_toBpu_redirect_bits_meta_commonHRMeta_attribute_2_branchType;
    logic [1:0] io_toBpu_redirect_bits_meta_commonHRMeta_attribute_3_branchType;
    logic [1:0] io_toBpu_redirect_bits_meta_commonHRMeta_attribute_4_branchType;
    logic [1:0] io_toBpu_redirect_bits_meta_commonHRMeta_attribute_5_branchType;
    logic [1:0] io_toBpu_redirect_bits_meta_commonHRMeta_attribute_6_branchType;
    logic [1:0] io_toBpu_redirect_bits_meta_commonHRMeta_attribute_7_branchType;
    logic [4:0] io_toBpu_redirect_bits_meta_commonHRMeta_position_0;
    logic [4:0] io_toBpu_redirect_bits_meta_commonHRMeta_position_1;
    logic [4:0] io_toBpu_redirect_bits_meta_commonHRMeta_position_2;
    logic [4:0] io_toBpu_redirect_bits_meta_commonHRMeta_position_3;
    logic [4:0] io_toBpu_redirect_bits_meta_commonHRMeta_position_4;
    logic [4:0] io_toBpu_redirect_bits_meta_commonHRMeta_position_5;
    logic [4:0] io_toBpu_redirect_bits_meta_commonHRMeta_position_6;
    logic [4:0] io_toBpu_redirect_bits_meta_commonHRMeta_position_7;
    logic [3:0] io_toBpu_redirect_bits_meta_ras_ssp;
    logic [2:0] io_toBpu_redirect_bits_meta_ras_sctr;
    logic io_toBpu_redirect_bits_meta_ras_tosw_flag;
    logic [4:0] io_toBpu_redirect_bits_meta_ras_tosw_value;
    logic io_toBpu_redirect_bits_meta_ras_tosr_flag;
    logic [4:0] io_toBpu_redirect_bits_meta_ras_tosr_value;
    logic io_toBpu_redirect_bits_meta_ras_nos_flag;
    logic [4:0] io_toBpu_redirect_bits_meta_ras_nos_value;
    logic io_toBpu_train_valid;
    logic [48:0] io_toBpu_train_bits_startPc_addr;
    logic io_toBpu_train_bits_branches_0_valid;
    logic [48:0] io_toBpu_train_bits_branches_0_bits_target_addr;
    logic io_toBpu_train_bits_branches_0_bits_taken;
    logic [4:0] io_toBpu_train_bits_branches_0_bits_cfiPosition;
    logic [1:0] io_toBpu_train_bits_branches_0_bits_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_branches_0_bits_attribute_rasAction;
    logic io_toBpu_train_bits_branches_0_bits_mispredict;
    logic io_toBpu_train_bits_branches_1_valid;
    logic [48:0] io_toBpu_train_bits_branches_1_bits_target_addr;
    logic io_toBpu_train_bits_branches_1_bits_taken;
    logic [4:0] io_toBpu_train_bits_branches_1_bits_cfiPosition;
    logic [1:0] io_toBpu_train_bits_branches_1_bits_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_branches_1_bits_attribute_rasAction;
    logic io_toBpu_train_bits_branches_1_bits_mispredict;
    logic io_toBpu_train_bits_branches_2_valid;
    logic [48:0] io_toBpu_train_bits_branches_2_bits_target_addr;
    logic io_toBpu_train_bits_branches_2_bits_taken;
    logic [4:0] io_toBpu_train_bits_branches_2_bits_cfiPosition;
    logic [1:0] io_toBpu_train_bits_branches_2_bits_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_branches_2_bits_attribute_rasAction;
    logic io_toBpu_train_bits_branches_2_bits_mispredict;
    logic io_toBpu_train_bits_branches_3_valid;
    logic [48:0] io_toBpu_train_bits_branches_3_bits_target_addr;
    logic io_toBpu_train_bits_branches_3_bits_taken;
    logic [4:0] io_toBpu_train_bits_branches_3_bits_cfiPosition;
    logic [1:0] io_toBpu_train_bits_branches_3_bits_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_branches_3_bits_attribute_rasAction;
    logic io_toBpu_train_bits_branches_3_bits_mispredict;
    logic io_toBpu_train_bits_branches_4_valid;
    logic [48:0] io_toBpu_train_bits_branches_4_bits_target_addr;
    logic io_toBpu_train_bits_branches_4_bits_taken;
    logic [4:0] io_toBpu_train_bits_branches_4_bits_cfiPosition;
    logic [1:0] io_toBpu_train_bits_branches_4_bits_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_branches_4_bits_attribute_rasAction;
    logic io_toBpu_train_bits_branches_4_bits_mispredict;
    logic io_toBpu_train_bits_branches_5_valid;
    logic [48:0] io_toBpu_train_bits_branches_5_bits_target_addr;
    logic io_toBpu_train_bits_branches_5_bits_taken;
    logic [4:0] io_toBpu_train_bits_branches_5_bits_cfiPosition;
    logic [1:0] io_toBpu_train_bits_branches_5_bits_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_branches_5_bits_attribute_rasAction;
    logic io_toBpu_train_bits_branches_5_bits_mispredict;
    logic io_toBpu_train_bits_branches_6_valid;
    logic [48:0] io_toBpu_train_bits_branches_6_bits_target_addr;
    logic io_toBpu_train_bits_branches_6_bits_taken;
    logic [4:0] io_toBpu_train_bits_branches_6_bits_cfiPosition;
    logic [1:0] io_toBpu_train_bits_branches_6_bits_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_branches_6_bits_attribute_rasAction;
    logic io_toBpu_train_bits_branches_6_bits_mispredict;
    logic io_toBpu_train_bits_branches_7_valid;
    logic [48:0] io_toBpu_train_bits_branches_7_bits_target_addr;
    logic io_toBpu_train_bits_branches_7_bits_taken;
    logic [4:0] io_toBpu_train_bits_branches_7_bits_cfiPosition;
    logic [1:0] io_toBpu_train_bits_branches_7_bits_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_branches_7_bits_attribute_rasAction;
    logic io_toBpu_train_bits_branches_7_bits_mispredict;
    logic io_toBpu_train_bits_meta_mbtb_entries_0_0_rawHit;
    logic [4:0] io_toBpu_train_bits_meta_mbtb_entries_0_0_position;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_0_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_0_attribute_rasAction;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_0_counter_value;
    logic io_toBpu_train_bits_meta_mbtb_entries_0_1_rawHit;
    logic [4:0] io_toBpu_train_bits_meta_mbtb_entries_0_1_position;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_1_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_1_attribute_rasAction;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_1_counter_value;
    logic io_toBpu_train_bits_meta_mbtb_entries_0_2_rawHit;
    logic [4:0] io_toBpu_train_bits_meta_mbtb_entries_0_2_position;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_2_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_2_attribute_rasAction;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_2_counter_value;
    logic io_toBpu_train_bits_meta_mbtb_entries_0_3_rawHit;
    logic [4:0] io_toBpu_train_bits_meta_mbtb_entries_0_3_position;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_3_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_3_attribute_rasAction;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_0_3_counter_value;
    logic io_toBpu_train_bits_meta_mbtb_entries_1_0_rawHit;
    logic [4:0] io_toBpu_train_bits_meta_mbtb_entries_1_0_position;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_0_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_0_attribute_rasAction;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_0_counter_value;
    logic io_toBpu_train_bits_meta_mbtb_entries_1_1_rawHit;
    logic [4:0] io_toBpu_train_bits_meta_mbtb_entries_1_1_position;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_1_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_1_attribute_rasAction;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_1_counter_value;
    logic io_toBpu_train_bits_meta_mbtb_entries_1_2_rawHit;
    logic [4:0] io_toBpu_train_bits_meta_mbtb_entries_1_2_position;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_2_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_2_attribute_rasAction;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_2_counter_value;
    logic io_toBpu_train_bits_meta_mbtb_entries_1_3_rawHit;
    logic [4:0] io_toBpu_train_bits_meta_mbtb_entries_1_3_position;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_3_attribute_branchType;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_3_attribute_rasAction;
    logic [1:0] io_toBpu_train_bits_meta_mbtb_entries_1_3_counter_value;
    logic io_toBpu_train_bits_meta_tage_entries_0_useProvider;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_0_providerTableIdx;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_0_providerWayIdx;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_0_providerTakenCtr_value;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_0_providerUsefulCtr_value;
    logic io_toBpu_train_bits_meta_tage_entries_0_altOrBasePred;
    logic io_toBpu_train_bits_meta_tage_entries_1_useProvider;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_1_providerTableIdx;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_1_providerWayIdx;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_1_providerTakenCtr_value;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_1_providerUsefulCtr_value;
    logic io_toBpu_train_bits_meta_tage_entries_1_altOrBasePred;
    logic io_toBpu_train_bits_meta_tage_entries_2_useProvider;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_2_providerTableIdx;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_2_providerWayIdx;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_2_providerTakenCtr_value;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_2_providerUsefulCtr_value;
    logic io_toBpu_train_bits_meta_tage_entries_2_altOrBasePred;
    logic io_toBpu_train_bits_meta_tage_entries_3_useProvider;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_3_providerTableIdx;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_3_providerWayIdx;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_3_providerTakenCtr_value;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_3_providerUsefulCtr_value;
    logic io_toBpu_train_bits_meta_tage_entries_3_altOrBasePred;
    logic io_toBpu_train_bits_meta_tage_entries_4_useProvider;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_4_providerTableIdx;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_4_providerWayIdx;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_4_providerTakenCtr_value;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_4_providerUsefulCtr_value;
    logic io_toBpu_train_bits_meta_tage_entries_4_altOrBasePred;
    logic io_toBpu_train_bits_meta_tage_entries_5_useProvider;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_5_providerTableIdx;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_5_providerWayIdx;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_5_providerTakenCtr_value;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_5_providerUsefulCtr_value;
    logic io_toBpu_train_bits_meta_tage_entries_5_altOrBasePred;
    logic io_toBpu_train_bits_meta_tage_entries_6_useProvider;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_6_providerTableIdx;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_6_providerWayIdx;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_6_providerTakenCtr_value;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_6_providerUsefulCtr_value;
    logic io_toBpu_train_bits_meta_tage_entries_6_altOrBasePred;
    logic io_toBpu_train_bits_meta_tage_entries_7_useProvider;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_7_providerTableIdx;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_7_providerWayIdx;
    logic [2:0] io_toBpu_train_bits_meta_tage_entries_7_providerTakenCtr_value;
    logic [1:0] io_toBpu_train_bits_meta_tage_entries_7_providerUsefulCtr_value;
    logic io_toBpu_train_bits_meta_tage_entries_7_altOrBasePred;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_0_0;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_0_1;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_0_2;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_0_3;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_0_4;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_0_5;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_0_6;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_0_7;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_1_0;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_1_1;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_1_2;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_1_3;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_1_4;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_1_5;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_1_6;
    logic [5:0] io_toBpu_train_bits_meta_sc_scPathResp_1_7;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_0;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_1;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_2;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_3;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_4;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_5;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_6;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_7;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_8;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_9;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_10;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_11;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_12;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_13;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_14;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_15;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_16;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_17;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_18;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_19;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_20;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_21;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_22;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_23;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_24;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_25;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_26;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_27;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_28;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_29;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_30;
    logic [5:0] io_toBpu_train_bits_meta_sc_scBiasResp_31;
    logic [1:0] io_toBpu_train_bits_meta_sc_scBiasLowerBits_0;
    logic [1:0] io_toBpu_train_bits_meta_sc_scBiasLowerBits_1;
    logic [1:0] io_toBpu_train_bits_meta_sc_scBiasLowerBits_2;
    logic [1:0] io_toBpu_train_bits_meta_sc_scBiasLowerBits_3;
    logic [1:0] io_toBpu_train_bits_meta_sc_scBiasLowerBits_4;
    logic [1:0] io_toBpu_train_bits_meta_sc_scBiasLowerBits_5;
    logic [1:0] io_toBpu_train_bits_meta_sc_scBiasLowerBits_6;
    logic [1:0] io_toBpu_train_bits_meta_sc_scBiasLowerBits_7;
    logic io_toBpu_train_bits_meta_sc_scCommonHR_valid;
    logic [15:0] io_toBpu_train_bits_meta_sc_scCommonHR_ghr;
    logic [7:0] io_toBpu_train_bits_meta_sc_scCommonHR_bw;
    logic io_toBpu_train_bits_meta_sc_scPred_0;
    logic io_toBpu_train_bits_meta_sc_scPred_1;
    logic io_toBpu_train_bits_meta_sc_scPred_2;
    logic io_toBpu_train_bits_meta_sc_scPred_3;
    logic io_toBpu_train_bits_meta_sc_scPred_4;
    logic io_toBpu_train_bits_meta_sc_scPred_5;
    logic io_toBpu_train_bits_meta_sc_scPred_6;
    logic io_toBpu_train_bits_meta_sc_scPred_7;
    logic io_toBpu_train_bits_meta_sc_tagePred_0;
    logic io_toBpu_train_bits_meta_sc_tagePred_1;
    logic io_toBpu_train_bits_meta_sc_tagePred_2;
    logic io_toBpu_train_bits_meta_sc_tagePred_3;
    logic io_toBpu_train_bits_meta_sc_tagePred_4;
    logic io_toBpu_train_bits_meta_sc_tagePred_5;
    logic io_toBpu_train_bits_meta_sc_tagePred_6;
    logic io_toBpu_train_bits_meta_sc_tagePred_7;
    logic io_toBpu_train_bits_meta_sc_tagePredValid_0;
    logic io_toBpu_train_bits_meta_sc_tagePredValid_1;
    logic io_toBpu_train_bits_meta_sc_tagePredValid_2;
    logic io_toBpu_train_bits_meta_sc_tagePredValid_3;
    logic io_toBpu_train_bits_meta_sc_tagePredValid_4;
    logic io_toBpu_train_bits_meta_sc_tagePredValid_5;
    logic io_toBpu_train_bits_meta_sc_tagePredValid_6;
    logic io_toBpu_train_bits_meta_sc_tagePredValid_7;
    logic io_toBpu_train_bits_meta_sc_useScPred_0;
    logic io_toBpu_train_bits_meta_sc_useScPred_1;
    logic io_toBpu_train_bits_meta_sc_useScPred_2;
    logic io_toBpu_train_bits_meta_sc_useScPred_3;
    logic io_toBpu_train_bits_meta_sc_useScPred_4;
    logic io_toBpu_train_bits_meta_sc_useScPred_5;
    logic io_toBpu_train_bits_meta_sc_useScPred_6;
    logic io_toBpu_train_bits_meta_sc_useScPred_7;
    logic io_toBpu_train_bits_meta_sc_sumAboveThres_0;
    logic io_toBpu_train_bits_meta_sc_sumAboveThres_1;
    logic io_toBpu_train_bits_meta_sc_sumAboveThres_2;
    logic io_toBpu_train_bits_meta_sc_sumAboveThres_3;
    logic io_toBpu_train_bits_meta_sc_sumAboveThres_4;
    logic io_toBpu_train_bits_meta_sc_sumAboveThres_5;
    logic io_toBpu_train_bits_meta_sc_sumAboveThres_6;
    logic io_toBpu_train_bits_meta_sc_sumAboveThres_7;
    logic io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_0;
    logic io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_1;
    logic io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_2;
    logic io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_3;
    logic io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_4;
    logic io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_5;
    logic io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_6;
    logic io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_7;
    logic io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_0;
    logic io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_1;
    logic io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_2;
    logic io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_3;
    logic io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_4;
    logic io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_5;
    logic io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_6;
    logic io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_7;
    logic [6:0] io_toBpu_train_bits_meta_sc_debug_predPathIdx_0;
    logic [6:0] io_toBpu_train_bits_meta_sc_debug_predPathIdx_1;
    logic [6:0] io_toBpu_train_bits_meta_sc_debug_predGlobalIdx_0;
    logic [6:0] io_toBpu_train_bits_meta_sc_debug_predGlobalIdx_1;
    logic [6:0] io_toBpu_train_bits_meta_sc_debug_predBWIdx_0;
    logic [6:0] io_toBpu_train_bits_meta_sc_debug_predBWIdx_1;
    logic [6:0] io_toBpu_train_bits_meta_sc_debug_predBiasIdx;
    logic io_toBpu_train_bits_meta_ittage_provider_valid;
    logic [2:0] io_toBpu_train_bits_meta_ittage_provider_bits;
    logic io_toBpu_train_bits_meta_ittage_altProvider_valid;
    logic [2:0] io_toBpu_train_bits_meta_ittage_altProvider_bits;
    logic io_toBpu_train_bits_meta_ittage_altDiffers;
    logic io_toBpu_train_bits_meta_ittage_providerUsefulCnt_value;
    logic [1:0] io_toBpu_train_bits_meta_ittage_providerCnt_value;
    logic [1:0] io_toBpu_train_bits_meta_ittage_altProviderCnt_value;
    logic io_toBpu_train_bits_meta_ittage_allocate_valid;
    logic [2:0] io_toBpu_train_bits_meta_ittage_allocate_bits;
    logic [48:0] io_toBpu_train_bits_meta_ittage_providerTarget_addr;
    logic [48:0] io_toBpu_train_bits_meta_ittage_altProviderTarget_addr;
    logic [9:0] io_toBpu_train_bits_meta_phr_phrPtr_value;
    logic [12:0] io_toBpu_train_bits_meta_phr_phrLowBits;
    logic [12:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist;
    logic [11:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist;
    logic [12:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist;
    logic [11:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist;
    logic [12:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist;
    logic [11:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist;
    logic [12:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist;
    logic [11:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist;
    logic [7:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist;
    logic [12:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist;
    logic [11:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist;
    logic [12:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist;
    logic [11:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist;
    logic [11:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist;
    logic [10:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist;
    logic [7:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist;
    logic [6:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist;
    logic [7:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist;
    logic [8:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist;
    logic [7:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist;
    logic [7:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist;
    logic [6:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist;
    logic [3:0] io_toBpu_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist;
    logic io_toBpu_commit_valid;
    logic [3:0] io_toBpu_commit_bits_meta_ras_ssp;
    logic io_toBpu_commit_bits_meta_ras_tosw_flag;
    logic [4:0] io_toBpu_commit_bits_meta_ras_tosw_value;
    logic [1:0] io_toBpu_commit_bits_attribute_rasAction;
    logic io_toBpu_bpuPtr_flag;
    logic [5:0] io_toBpu_bpuPtr_value;
    logic io_fromIfu_mmioCommitRead_mmioLastCommit;
    logic io_toIfu_req_valid;
    logic io_toIfu_req_bits_fetch_0_valid;
    logic [48:0] io_toIfu_req_bits_fetch_0_startVAddr_addr;
    logic [48:0] io_toIfu_req_bits_fetch_0_nextStartVAddr_addr;
    logic io_toIfu_req_bits_fetch_0_ftqIdx_flag;
    logic [5:0] io_toIfu_req_bits_fetch_0_ftqIdx_value;
    logic io_toIfu_req_bits_fetch_0_takenCfiOffset_valid;
    logic [4:0] io_toIfu_req_bits_fetch_0_takenCfiOffset_bits;
    logic io_toIfu_req_bits_topdownInfo_reasons_7;
    logic io_toIfu_redirect_valid;
    logic io_toIfu_topdownRedirect_valid;
    logic io_toIfu_topdownRedirect_bits_debugIsCtrl;
    logic io_toIfu_topdownRedirect_bits_debugIsMemVio;
    logic io_toIfu_flushFromBpu_s3_valid;
    logic io_toIfu_flushFromBpu_s3_bits_flag;
    logic [5:0] io_toIfu_flushFromBpu_s3_bits_value;
    logic io_toICache_fetchReq_valid;
    logic [48:0] io_toICache_fetchReq_bits_startVAddr_addr;
    logic [48:0] io_toICache_fetchReq_bits_nextCachelineVAddr_addr;
    logic io_toICache_fetchReq_bits_ftqIdx_flag;
    logic [5:0] io_toICache_fetchReq_bits_ftqIdx_value;
    logic [4:0] io_toICache_fetchReq_bits_takenCfiOffset;
    logic io_toICache_fetchReq_bits_isBackendException;
    logic io_toICache_prefetchReq_valid;
    logic [48:0] io_toICache_prefetchReq_bits_startVAddr_addr;
    logic [48:0] io_toICache_prefetchReq_bits_nextCachelineVAddr_addr;
    logic io_toICache_prefetchReq_bits_ftqIdx_flag;
    logic [5:0] io_toICache_prefetchReq_bits_ftqIdx_value;
    logic [4:0] io_toICache_prefetchReq_bits_takenCfiOffset;
    logic [2:0] io_toICache_prefetchReq_bits_backendException_value;
    logic io_toICache_flushFromBpu_s3_valid;
    logic io_toICache_flushFromBpu_s3_bits_flag;
    logic [5:0] io_toICache_flushFromBpu_s3_bits_value;
    logic io_toICache_redirectFlush;
    logic io_toBackend_wen;
    logic [5:0] io_toBackend_ftqIdx;
    logic [48:0] io_toBackend_startPc_addr;
    logic io_bpuTopDownInfo_tageMissBubble;
    logic io_bpuTopDownInfo_ittageMissBubble;
    logic io_bpuTopDownInfo_rasMissBubble;

    Ftq dut (
        .clock(clk),
        .reset(reset),
        .io_fromBpu_prediction_valid(io_fromBpu_prediction_valid),
        .io_fromBpu_prediction_bits_startPc_addr(io_fromBpu_prediction_bits_startPc_addr),
        .io_fromBpu_prediction_bits_target_addr(io_fromBpu_prediction_bits_target_addr),
        .io_fromBpu_prediction_bits_takenCfiOffset_valid(io_fromBpu_prediction_bits_takenCfiOffset_valid),
        .io_fromBpu_prediction_bits_takenCfiOffset_bits(io_fromBpu_prediction_bits_takenCfiOffset_bits),
        .io_fromBpu_prediction_bits_s3Override(io_fromBpu_prediction_bits_s3Override),
        .io_fromBpu_meta_valid(io_fromBpu_meta_valid),
        .io_fromBpu_meta_bits_redirectMeta_phr_phrPtr_flag(io_fromBpu_meta_bits_redirectMeta_phr_phrPtr_flag),
        .io_fromBpu_meta_bits_redirectMeta_phr_phrPtr_value(io_fromBpu_meta_bits_redirectMeta_phr_phrPtr_value),
        .io_fromBpu_meta_bits_redirectMeta_phr_phrLowBits(io_fromBpu_meta_bits_redirectMeta_phr_phrLowBits),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_ghr(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_ghr),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_bw(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_bw),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_0(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_0),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_1(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_1),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_2(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_2),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_3(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_3),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_4(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_4),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_5(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_5),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_6(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_6),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_7(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_hitMask_7),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_0_branchType(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_0_branchType),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_1_branchType(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_1_branchType),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_2_branchType(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_2_branchType),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_3_branchType(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_3_branchType),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_4_branchType(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_4_branchType),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_5_branchType(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_5_branchType),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_6_branchType(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_6_branchType),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_7_branchType(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_attribute_7_branchType),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_0(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_0),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_1(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_1),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_2(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_2),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_3(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_3),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_4(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_4),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_5(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_5),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_6(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_6),
        .io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_7(io_fromBpu_meta_bits_redirectMeta_commonHRMeta_position_7),
        .io_fromBpu_meta_bits_redirectMeta_ras_ssp(io_fromBpu_meta_bits_redirectMeta_ras_ssp),
        .io_fromBpu_meta_bits_redirectMeta_ras_sctr(io_fromBpu_meta_bits_redirectMeta_ras_sctr),
        .io_fromBpu_meta_bits_redirectMeta_ras_tosw_flag(io_fromBpu_meta_bits_redirectMeta_ras_tosw_flag),
        .io_fromBpu_meta_bits_redirectMeta_ras_tosw_value(io_fromBpu_meta_bits_redirectMeta_ras_tosw_value),
        .io_fromBpu_meta_bits_redirectMeta_ras_tosr_flag(io_fromBpu_meta_bits_redirectMeta_ras_tosr_flag),
        .io_fromBpu_meta_bits_redirectMeta_ras_tosr_value(io_fromBpu_meta_bits_redirectMeta_ras_tosr_value),
        .io_fromBpu_meta_bits_redirectMeta_ras_nos_flag(io_fromBpu_meta_bits_redirectMeta_ras_nos_flag),
        .io_fromBpu_meta_bits_redirectMeta_ras_nos_value(io_fromBpu_meta_bits_redirectMeta_ras_nos_value),
        .io_fromBpu_meta_bits_redirectMeta_ras_topRetAddr_addr(io_fromBpu_meta_bits_redirectMeta_ras_topRetAddr_addr),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_rawHit(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_rawHit),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_position(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_position),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_branchType(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_branchType),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_rasAction(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_rasAction),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_counter_value(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_0_counter_value),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_rawHit(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_rawHit),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_position(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_position),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_branchType(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_branchType),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_rasAction(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_rasAction),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_counter_value(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_1_counter_value),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_rawHit(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_rawHit),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_position(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_position),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_branchType(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_branchType),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_rasAction(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_rasAction),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_counter_value(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_2_counter_value),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_rawHit(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_rawHit),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_position(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_position),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_branchType(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_branchType),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_rasAction(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_rasAction),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_counter_value(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_0_3_counter_value),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_rawHit(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_rawHit),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_position(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_position),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_branchType(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_branchType),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_rasAction(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_rasAction),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_counter_value(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_0_counter_value),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_rawHit(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_rawHit),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_position(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_position),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_branchType(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_branchType),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_rasAction(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_rasAction),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_counter_value(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_1_counter_value),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_rawHit(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_rawHit),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_position(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_position),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_branchType(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_branchType),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_rasAction(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_rasAction),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_counter_value(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_2_counter_value),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_rawHit(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_rawHit),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_position(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_position),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_branchType(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_branchType),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_rasAction(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_rasAction),
        .io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_counter_value(io_fromBpu_meta_bits_resolveMeta_mbtb_entries_1_3_counter_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_0_useProvider(io_fromBpu_meta_bits_resolveMeta_tage_entries_0_useProvider),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerTableIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerTableIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerWayIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerWayIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerTakenCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerTakenCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerUsefulCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_0_providerUsefulCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_0_altOrBasePred(io_fromBpu_meta_bits_resolveMeta_tage_entries_0_altOrBasePred),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_1_useProvider(io_fromBpu_meta_bits_resolveMeta_tage_entries_1_useProvider),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerTableIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerTableIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerWayIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerWayIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerTakenCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerTakenCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerUsefulCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_1_providerUsefulCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_1_altOrBasePred(io_fromBpu_meta_bits_resolveMeta_tage_entries_1_altOrBasePred),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_2_useProvider(io_fromBpu_meta_bits_resolveMeta_tage_entries_2_useProvider),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerTableIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerTableIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerWayIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerWayIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerTakenCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerTakenCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerUsefulCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_2_providerUsefulCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_2_altOrBasePred(io_fromBpu_meta_bits_resolveMeta_tage_entries_2_altOrBasePred),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_3_useProvider(io_fromBpu_meta_bits_resolveMeta_tage_entries_3_useProvider),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerTableIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerTableIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerWayIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerWayIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerTakenCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerTakenCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerUsefulCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_3_providerUsefulCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_3_altOrBasePred(io_fromBpu_meta_bits_resolveMeta_tage_entries_3_altOrBasePred),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_4_useProvider(io_fromBpu_meta_bits_resolveMeta_tage_entries_4_useProvider),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerTableIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerTableIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerWayIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerWayIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerTakenCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerTakenCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerUsefulCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_4_providerUsefulCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_4_altOrBasePred(io_fromBpu_meta_bits_resolveMeta_tage_entries_4_altOrBasePred),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_5_useProvider(io_fromBpu_meta_bits_resolveMeta_tage_entries_5_useProvider),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerTableIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerTableIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerWayIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerWayIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerTakenCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerTakenCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerUsefulCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_5_providerUsefulCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_5_altOrBasePred(io_fromBpu_meta_bits_resolveMeta_tage_entries_5_altOrBasePred),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_6_useProvider(io_fromBpu_meta_bits_resolveMeta_tage_entries_6_useProvider),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerTableIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerTableIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerWayIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerWayIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerTakenCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerTakenCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerUsefulCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_6_providerUsefulCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_6_altOrBasePred(io_fromBpu_meta_bits_resolveMeta_tage_entries_6_altOrBasePred),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_7_useProvider(io_fromBpu_meta_bits_resolveMeta_tage_entries_7_useProvider),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerTableIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerTableIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerWayIdx(io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerWayIdx),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerTakenCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerTakenCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerUsefulCtr_value(io_fromBpu_meta_bits_resolveMeta_tage_entries_7_providerUsefulCtr_value),
        .io_fromBpu_meta_bits_resolveMeta_tage_entries_7_altOrBasePred(io_fromBpu_meta_bits_resolveMeta_tage_entries_7_altOrBasePred),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_0(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_1(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_2(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_3(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_4(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_5(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_6(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_7(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_0_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_0(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_1(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_2(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_3(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_4(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_5(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_6(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_7(io_fromBpu_meta_bits_resolveMeta_sc_scPathResp_1_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_0(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_1(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_2(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_3(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_4(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_5(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_6(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_7(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_8(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_8),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_9(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_9),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_10(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_10),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_11(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_11),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_12(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_12),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_13(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_13),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_14(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_14),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_15(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_15),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_16(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_16),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_17(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_17),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_18(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_18),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_19(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_19),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_20(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_20),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_21(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_21),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_22(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_22),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_23(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_23),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_24(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_24),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_25(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_25),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_26(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_26),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_27(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_27),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_28(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_28),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_29(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_29),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_30(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_30),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_31(io_fromBpu_meta_bits_resolveMeta_sc_scBiasResp_31),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_0(io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_1(io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_2(io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_3(io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_4(io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_5(io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_6(io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_7(io_fromBpu_meta_bits_resolveMeta_sc_scBiasLowerBits_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_scCommonHR_valid(io_fromBpu_meta_bits_resolveMeta_sc_scCommonHR_valid),
        .io_fromBpu_meta_bits_resolveMeta_sc_scCommonHR_ghr(io_fromBpu_meta_bits_resolveMeta_sc_scCommonHR_ghr),
        .io_fromBpu_meta_bits_resolveMeta_sc_scCommonHR_bw(io_fromBpu_meta_bits_resolveMeta_sc_scCommonHR_bw),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPred_0(io_fromBpu_meta_bits_resolveMeta_sc_scPred_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPred_1(io_fromBpu_meta_bits_resolveMeta_sc_scPred_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPred_2(io_fromBpu_meta_bits_resolveMeta_sc_scPred_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPred_3(io_fromBpu_meta_bits_resolveMeta_sc_scPred_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPred_4(io_fromBpu_meta_bits_resolveMeta_sc_scPred_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPred_5(io_fromBpu_meta_bits_resolveMeta_sc_scPred_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPred_6(io_fromBpu_meta_bits_resolveMeta_sc_scPred_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_scPred_7(io_fromBpu_meta_bits_resolveMeta_sc_scPred_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePred_0(io_fromBpu_meta_bits_resolveMeta_sc_tagePred_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePred_1(io_fromBpu_meta_bits_resolveMeta_sc_tagePred_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePred_2(io_fromBpu_meta_bits_resolveMeta_sc_tagePred_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePred_3(io_fromBpu_meta_bits_resolveMeta_sc_tagePred_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePred_4(io_fromBpu_meta_bits_resolveMeta_sc_tagePred_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePred_5(io_fromBpu_meta_bits_resolveMeta_sc_tagePred_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePred_6(io_fromBpu_meta_bits_resolveMeta_sc_tagePred_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePred_7(io_fromBpu_meta_bits_resolveMeta_sc_tagePred_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_0(io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_1(io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_2(io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_3(io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_4(io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_5(io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_6(io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_7(io_fromBpu_meta_bits_resolveMeta_sc_tagePredValid_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_useScPred_0(io_fromBpu_meta_bits_resolveMeta_sc_useScPred_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_useScPred_1(io_fromBpu_meta_bits_resolveMeta_sc_useScPred_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_useScPred_2(io_fromBpu_meta_bits_resolveMeta_sc_useScPred_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_useScPred_3(io_fromBpu_meta_bits_resolveMeta_sc_useScPred_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_useScPred_4(io_fromBpu_meta_bits_resolveMeta_sc_useScPred_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_useScPred_5(io_fromBpu_meta_bits_resolveMeta_sc_useScPred_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_useScPred_6(io_fromBpu_meta_bits_resolveMeta_sc_useScPred_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_useScPred_7(io_fromBpu_meta_bits_resolveMeta_sc_useScPred_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_0(io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_1(io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_2(io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_3(io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_4(io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_5(io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_6(io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_7(io_fromBpu_meta_bits_resolveMeta_sc_sumAboveThres_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_0(io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_1(io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_2(io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_3(io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_4(io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_5(io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_6(io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_7(io_fromBpu_meta_bits_resolveMeta_sc_debug_scPathTakenVec_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_0(io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_1(io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_2(io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_2),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_3(io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_3),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_4(io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_4),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_5(io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_5),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_6(io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_6),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_7(io_fromBpu_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_7),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_predPathIdx_0(io_fromBpu_meta_bits_resolveMeta_sc_debug_predPathIdx_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_predPathIdx_1(io_fromBpu_meta_bits_resolveMeta_sc_debug_predPathIdx_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_predGlobalIdx_0(io_fromBpu_meta_bits_resolveMeta_sc_debug_predGlobalIdx_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_predGlobalIdx_1(io_fromBpu_meta_bits_resolveMeta_sc_debug_predGlobalIdx_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_predBWIdx_0(io_fromBpu_meta_bits_resolveMeta_sc_debug_predBWIdx_0),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_predBWIdx_1(io_fromBpu_meta_bits_resolveMeta_sc_debug_predBWIdx_1),
        .io_fromBpu_meta_bits_resolveMeta_sc_debug_predBiasIdx(io_fromBpu_meta_bits_resolveMeta_sc_debug_predBiasIdx),
        .io_fromBpu_meta_bits_resolveMeta_ittage_provider_valid(io_fromBpu_meta_bits_resolveMeta_ittage_provider_valid),
        .io_fromBpu_meta_bits_resolveMeta_ittage_provider_bits(io_fromBpu_meta_bits_resolveMeta_ittage_provider_bits),
        .io_fromBpu_meta_bits_resolveMeta_ittage_altProvider_valid(io_fromBpu_meta_bits_resolveMeta_ittage_altProvider_valid),
        .io_fromBpu_meta_bits_resolveMeta_ittage_altProvider_bits(io_fromBpu_meta_bits_resolveMeta_ittage_altProvider_bits),
        .io_fromBpu_meta_bits_resolveMeta_ittage_altDiffers(io_fromBpu_meta_bits_resolveMeta_ittage_altDiffers),
        .io_fromBpu_meta_bits_resolveMeta_ittage_providerUsefulCnt_value(io_fromBpu_meta_bits_resolveMeta_ittage_providerUsefulCnt_value),
        .io_fromBpu_meta_bits_resolveMeta_ittage_providerCnt_value(io_fromBpu_meta_bits_resolveMeta_ittage_providerCnt_value),
        .io_fromBpu_meta_bits_resolveMeta_ittage_altProviderCnt_value(io_fromBpu_meta_bits_resolveMeta_ittage_altProviderCnt_value),
        .io_fromBpu_meta_bits_resolveMeta_ittage_allocate_valid(io_fromBpu_meta_bits_resolveMeta_ittage_allocate_valid),
        .io_fromBpu_meta_bits_resolveMeta_ittage_allocate_bits(io_fromBpu_meta_bits_resolveMeta_ittage_allocate_bits),
        .io_fromBpu_meta_bits_resolveMeta_ittage_providerTarget_addr(io_fromBpu_meta_bits_resolveMeta_ittage_providerTarget_addr),
        .io_fromBpu_meta_bits_resolveMeta_ittage_altProviderTarget_addr(io_fromBpu_meta_bits_resolveMeta_ittage_altProviderTarget_addr),
        .io_fromBpu_meta_bits_resolveMeta_phr_phrPtr_value(io_fromBpu_meta_bits_resolveMeta_phr_phrPtr_value),
        .io_fromBpu_meta_bits_resolveMeta_phr_phrLowBits(io_fromBpu_meta_bits_resolveMeta_phr_phrLowBits),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_31_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_31_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_30_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_30_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_29_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_29_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_28_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_28_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_27_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_27_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_26_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_26_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_25_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_25_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_24_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_24_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_23_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_23_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_22_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_22_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_21_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_21_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_20_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_20_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_19_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_19_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_18_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_18_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_17_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_17_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_16_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_16_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_15_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_15_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_14_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_14_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_13_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_13_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_12_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_12_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_11_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_11_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_10_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_10_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_9_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_9_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_8_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_8_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_7_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_7_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_6_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_6_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_5_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_5_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_4_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_4_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_3_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_3_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_2_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_2_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_1_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_1_foldedHist),
        .io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_0_foldedHist(io_fromBpu_meta_bits_resolveMeta_phr_predFoldedHist_hist_0_foldedHist),
        .io_fromBpu_meta_bits_commitMeta_ras_ssp(io_fromBpu_meta_bits_commitMeta_ras_ssp),
        .io_fromBpu_meta_bits_commitMeta_ras_tosw_flag(io_fromBpu_meta_bits_commitMeta_ras_tosw_flag),
        .io_fromBpu_meta_bits_commitMeta_ras_tosw_value(io_fromBpu_meta_bits_commitMeta_ras_tosw_value),
        .io_fromBpu_s3FtqPtr_flag(io_fromBpu_s3FtqPtr_flag),
        .io_fromBpu_s3FtqPtr_value(io_fromBpu_s3FtqPtr_value),
        .io_fromBpu_perfMeta_s1Prediction_cfiPosition(io_fromBpu_perfMeta_s1Prediction_cfiPosition),
        .io_fromBpu_perfMeta_s1Prediction_target_addr(io_fromBpu_perfMeta_s1Prediction_target_addr),
        .io_fromBpu_perfMeta_s1Prediction_attribute_branchType(io_fromBpu_perfMeta_s1Prediction_attribute_branchType),
        .io_fromBpu_perfMeta_s1Prediction_attribute_rasAction(io_fromBpu_perfMeta_s1Prediction_attribute_rasAction),
        .io_fromBpu_perfMeta_s1Prediction_taken(io_fromBpu_perfMeta_s1Prediction_taken),
        .io_fromBpu_perfMeta_s3Prediction_cfiPosition(io_fromBpu_perfMeta_s3Prediction_cfiPosition),
        .io_fromBpu_perfMeta_s3Prediction_target_addr(io_fromBpu_perfMeta_s3Prediction_target_addr),
        .io_fromBpu_perfMeta_s3Prediction_attribute_branchType(io_fromBpu_perfMeta_s3Prediction_attribute_branchType),
        .io_fromBpu_perfMeta_s3Prediction_attribute_rasAction(io_fromBpu_perfMeta_s3Prediction_attribute_rasAction),
        .io_fromBpu_perfMeta_s3Prediction_taken(io_fromBpu_perfMeta_s3Prediction_taken),
        .io_fromBpu_perfMeta_mbtbMeta_entries_0_0_rawHit(io_fromBpu_perfMeta_mbtbMeta_entries_0_0_rawHit),
        .io_fromBpu_perfMeta_mbtbMeta_entries_0_0_position(io_fromBpu_perfMeta_mbtbMeta_entries_0_0_position),
        .io_fromBpu_perfMeta_mbtbMeta_entries_0_1_rawHit(io_fromBpu_perfMeta_mbtbMeta_entries_0_1_rawHit),
        .io_fromBpu_perfMeta_mbtbMeta_entries_0_1_position(io_fromBpu_perfMeta_mbtbMeta_entries_0_1_position),
        .io_fromBpu_perfMeta_mbtbMeta_entries_0_2_rawHit(io_fromBpu_perfMeta_mbtbMeta_entries_0_2_rawHit),
        .io_fromBpu_perfMeta_mbtbMeta_entries_0_2_position(io_fromBpu_perfMeta_mbtbMeta_entries_0_2_position),
        .io_fromBpu_perfMeta_mbtbMeta_entries_0_3_rawHit(io_fromBpu_perfMeta_mbtbMeta_entries_0_3_rawHit),
        .io_fromBpu_perfMeta_mbtbMeta_entries_0_3_position(io_fromBpu_perfMeta_mbtbMeta_entries_0_3_position),
        .io_fromBpu_perfMeta_mbtbMeta_entries_1_0_rawHit(io_fromBpu_perfMeta_mbtbMeta_entries_1_0_rawHit),
        .io_fromBpu_perfMeta_mbtbMeta_entries_1_0_position(io_fromBpu_perfMeta_mbtbMeta_entries_1_0_position),
        .io_fromBpu_perfMeta_mbtbMeta_entries_1_1_rawHit(io_fromBpu_perfMeta_mbtbMeta_entries_1_1_rawHit),
        .io_fromBpu_perfMeta_mbtbMeta_entries_1_1_position(io_fromBpu_perfMeta_mbtbMeta_entries_1_1_position),
        .io_fromBpu_perfMeta_mbtbMeta_entries_1_2_rawHit(io_fromBpu_perfMeta_mbtbMeta_entries_1_2_rawHit),
        .io_fromBpu_perfMeta_mbtbMeta_entries_1_2_position(io_fromBpu_perfMeta_mbtbMeta_entries_1_2_position),
        .io_fromBpu_perfMeta_mbtbMeta_entries_1_3_rawHit(io_fromBpu_perfMeta_mbtbMeta_entries_1_3_rawHit),
        .io_fromBpu_perfMeta_mbtbMeta_entries_1_3_position(io_fromBpu_perfMeta_mbtbMeta_entries_1_3_position),
        .io_fromBpu_perfMeta_bpSource_s1Source(io_fromBpu_perfMeta_bpSource_s1Source),
        .io_fromBpu_perfMeta_bpSource_s3Source(io_fromBpu_perfMeta_bpSource_s3Source),
        .io_fromBpu_perfMeta_bpSource_s3Override(io_fromBpu_perfMeta_bpSource_s3Override),
        .io_toBpu_train_ready(io_toBpu_train_ready),
        .io_fromIfu_mmioCommitRead_valid(io_fromIfu_mmioCommitRead_valid),
        .io_fromIfu_mmioCommitRead_mmioFtqPtr_flag(io_fromIfu_mmioCommitRead_mmioFtqPtr_flag),
        .io_fromIfu_mmioCommitRead_mmioFtqPtr_value(io_fromIfu_mmioCommitRead_mmioFtqPtr_value),
        .io_fromIfu_wbRedirect_valid(io_fromIfu_wbRedirect_valid),
        .io_fromIfu_wbRedirect_bits_ftqIdx_flag(io_fromIfu_wbRedirect_bits_ftqIdx_flag),
        .io_fromIfu_wbRedirect_bits_ftqIdx_value(io_fromIfu_wbRedirect_bits_ftqIdx_value),
        .io_fromIfu_wbRedirect_bits_pc(io_fromIfu_wbRedirect_bits_pc),
        .io_fromIfu_wbRedirect_bits_taken(io_fromIfu_wbRedirect_bits_taken),
        .io_fromIfu_wbRedirect_bits_ftqOffset(io_fromIfu_wbRedirect_bits_ftqOffset),
        .io_fromIfu_wbRedirect_bits_isRVC(io_fromIfu_wbRedirect_bits_isRVC),
        .io_fromIfu_wbRedirect_bits_attribute_branchType(io_fromIfu_wbRedirect_bits_attribute_branchType),
        .io_fromIfu_wbRedirect_bits_attribute_rasAction(io_fromIfu_wbRedirect_bits_attribute_rasAction),
        .io_fromIfu_wbRedirect_bits_target(io_fromIfu_wbRedirect_bits_target),
        .io_toIfu_req_ready(io_toIfu_req_ready),
        .io_toICache_prefetchReq_ready(io_toICache_prefetchReq_ready),
        .io_fromBackend_redirect_valid(io_fromBackend_redirect_valid),
        .io_fromBackend_redirect_bits_ftqIdx_flag(io_fromBackend_redirect_bits_ftqIdx_flag),
        .io_fromBackend_redirect_bits_ftqIdx_value(io_fromBackend_redirect_bits_ftqIdx_value),
        .io_fromBackend_redirect_bits_pc(io_fromBackend_redirect_bits_pc),
        .io_fromBackend_redirect_bits_taken(io_fromBackend_redirect_bits_taken),
        .io_fromBackend_redirect_bits_ftqOffset(io_fromBackend_redirect_bits_ftqOffset),
        .io_fromBackend_redirect_bits_isRVC(io_fromBackend_redirect_bits_isRVC),
        .io_fromBackend_redirect_bits_attribute_branchType(io_fromBackend_redirect_bits_attribute_branchType),
        .io_fromBackend_redirect_bits_attribute_rasAction(io_fromBackend_redirect_bits_attribute_rasAction),
        .io_fromBackend_redirect_bits_target(io_fromBackend_redirect_bits_target),
        .io_fromBackend_redirect_bits_level(io_fromBackend_redirect_bits_level),
        .io_fromBackend_redirect_bits_backendIGPF(io_fromBackend_redirect_bits_backendIGPF),
        .io_fromBackend_redirect_bits_backendIPF(io_fromBackend_redirect_bits_backendIPF),
        .io_fromBackend_redirect_bits_backendIAF(io_fromBackend_redirect_bits_backendIAF),
        .io_fromBackend_redirect_bits_isMisPred(io_fromBackend_redirect_bits_isMisPred),
        .io_fromBackend_redirect_bits_debugIsCtrl(io_fromBackend_redirect_bits_debugIsCtrl),
        .io_fromBackend_redirect_bits_debugIsMemVio(io_fromBackend_redirect_bits_debugIsMemVio),
        .io_fromBackend_ftqIdxAhead_0_valid(io_fromBackend_ftqIdxAhead_0_valid),
        .io_fromBackend_ftqIdxAhead_0_bits_value(io_fromBackend_ftqIdxAhead_0_bits_value),
        .io_fromBackend_resolve_0_valid(io_fromBackend_resolve_0_valid),
        .io_fromBackend_resolve_0_bits_ftqIdx_flag(io_fromBackend_resolve_0_bits_ftqIdx_flag),
        .io_fromBackend_resolve_0_bits_ftqIdx_value(io_fromBackend_resolve_0_bits_ftqIdx_value),
        .io_fromBackend_resolve_0_bits_ftqOffset(io_fromBackend_resolve_0_bits_ftqOffset),
        .io_fromBackend_resolve_0_bits_pc_addr(io_fromBackend_resolve_0_bits_pc_addr),
        .io_fromBackend_resolve_0_bits_target_addr(io_fromBackend_resolve_0_bits_target_addr),
        .io_fromBackend_resolve_0_bits_taken(io_fromBackend_resolve_0_bits_taken),
        .io_fromBackend_resolve_0_bits_mispredict(io_fromBackend_resolve_0_bits_mispredict),
        .io_fromBackend_resolve_0_bits_attribute_branchType(io_fromBackend_resolve_0_bits_attribute_branchType),
        .io_fromBackend_resolve_0_bits_attribute_rasAction(io_fromBackend_resolve_0_bits_attribute_rasAction),
        .io_fromBackend_resolve_1_valid(io_fromBackend_resolve_1_valid),
        .io_fromBackend_resolve_1_bits_ftqIdx_flag(io_fromBackend_resolve_1_bits_ftqIdx_flag),
        .io_fromBackend_resolve_1_bits_ftqIdx_value(io_fromBackend_resolve_1_bits_ftqIdx_value),
        .io_fromBackend_resolve_1_bits_ftqOffset(io_fromBackend_resolve_1_bits_ftqOffset),
        .io_fromBackend_resolve_1_bits_pc_addr(io_fromBackend_resolve_1_bits_pc_addr),
        .io_fromBackend_resolve_1_bits_target_addr(io_fromBackend_resolve_1_bits_target_addr),
        .io_fromBackend_resolve_1_bits_taken(io_fromBackend_resolve_1_bits_taken),
        .io_fromBackend_resolve_1_bits_mispredict(io_fromBackend_resolve_1_bits_mispredict),
        .io_fromBackend_resolve_1_bits_attribute_branchType(io_fromBackend_resolve_1_bits_attribute_branchType),
        .io_fromBackend_resolve_1_bits_attribute_rasAction(io_fromBackend_resolve_1_bits_attribute_rasAction),
        .io_fromBackend_resolve_2_valid(io_fromBackend_resolve_2_valid),
        .io_fromBackend_resolve_2_bits_ftqIdx_flag(io_fromBackend_resolve_2_bits_ftqIdx_flag),
        .io_fromBackend_resolve_2_bits_ftqIdx_value(io_fromBackend_resolve_2_bits_ftqIdx_value),
        .io_fromBackend_resolve_2_bits_ftqOffset(io_fromBackend_resolve_2_bits_ftqOffset),
        .io_fromBackend_resolve_2_bits_pc_addr(io_fromBackend_resolve_2_bits_pc_addr),
        .io_fromBackend_resolve_2_bits_target_addr(io_fromBackend_resolve_2_bits_target_addr),
        .io_fromBackend_resolve_2_bits_taken(io_fromBackend_resolve_2_bits_taken),
        .io_fromBackend_resolve_2_bits_mispredict(io_fromBackend_resolve_2_bits_mispredict),
        .io_fromBackend_resolve_2_bits_attribute_branchType(io_fromBackend_resolve_2_bits_attribute_branchType),
        .io_fromBackend_resolve_2_bits_attribute_rasAction(io_fromBackend_resolve_2_bits_attribute_rasAction),
        .io_fromBackend_commit_valid(io_fromBackend_commit_valid),
        .io_fromBackend_commit_bits_flag(io_fromBackend_commit_bits_flag),
        .io_fromBackend_commit_bits_value(io_fromBackend_commit_bits_value),
        .io_fromBackend_callRetCommit_0_valid(io_fromBackend_callRetCommit_0_valid),
        .io_fromBackend_callRetCommit_0_bits_rasAction(io_fromBackend_callRetCommit_0_bits_rasAction),
        .io_fromBackend_callRetCommit_0_bits_ftqPtr_value(io_fromBackend_callRetCommit_0_bits_ftqPtr_value),
        .io_fromBackend_callRetCommit_1_valid(io_fromBackend_callRetCommit_1_valid),
        .io_fromBackend_callRetCommit_1_bits_rasAction(io_fromBackend_callRetCommit_1_bits_rasAction),
        .io_fromBackend_callRetCommit_1_bits_ftqPtr_value(io_fromBackend_callRetCommit_1_bits_ftqPtr_value),
        .io_fromBackend_callRetCommit_2_valid(io_fromBackend_callRetCommit_2_valid),
        .io_fromBackend_callRetCommit_2_bits_rasAction(io_fromBackend_callRetCommit_2_bits_rasAction),
        .io_fromBackend_callRetCommit_2_bits_ftqPtr_value(io_fromBackend_callRetCommit_2_bits_ftqPtr_value),
        .io_fromBackend_callRetCommit_3_valid(io_fromBackend_callRetCommit_3_valid),
        .io_fromBackend_callRetCommit_3_bits_rasAction(io_fromBackend_callRetCommit_3_bits_rasAction),
        .io_fromBackend_callRetCommit_3_bits_ftqPtr_value(io_fromBackend_callRetCommit_3_bits_ftqPtr_value),
        .io_fromBackend_callRetCommit_4_valid(io_fromBackend_callRetCommit_4_valid),
        .io_fromBackend_callRetCommit_4_bits_rasAction(io_fromBackend_callRetCommit_4_bits_rasAction),
        .io_fromBackend_callRetCommit_4_bits_ftqPtr_value(io_fromBackend_callRetCommit_4_bits_ftqPtr_value),
        .io_fromBackend_callRetCommit_5_valid(io_fromBackend_callRetCommit_5_valid),
        .io_fromBackend_callRetCommit_5_bits_rasAction(io_fromBackend_callRetCommit_5_bits_rasAction),
        .io_fromBackend_callRetCommit_5_bits_ftqPtr_value(io_fromBackend_callRetCommit_5_bits_ftqPtr_value),
        .io_fromBackend_callRetCommit_6_valid(io_fromBackend_callRetCommit_6_valid),
        .io_fromBackend_callRetCommit_6_bits_rasAction(io_fromBackend_callRetCommit_6_bits_rasAction),
        .io_fromBackend_callRetCommit_6_bits_ftqPtr_value(io_fromBackend_callRetCommit_6_bits_ftqPtr_value),
        .io_fromBackend_callRetCommit_7_valid(io_fromBackend_callRetCommit_7_valid),
        .io_fromBackend_callRetCommit_7_bits_rasAction(io_fromBackend_callRetCommit_7_bits_rasAction),
        .io_fromBackend_callRetCommit_7_bits_ftqPtr_value(io_fromBackend_callRetCommit_7_bits_ftqPtr_value),
        .io_fromBpu_prediction_ready(io_fromBpu_prediction_ready),
        .io_toBpu_redirect_valid(io_toBpu_redirect_valid),
        .io_toBpu_redirect_bits_cfiPc_addr(io_toBpu_redirect_bits_cfiPc_addr),
        .io_toBpu_redirect_bits_target_addr(io_toBpu_redirect_bits_target_addr),
        .io_toBpu_redirect_bits_taken(io_toBpu_redirect_bits_taken),
        .io_toBpu_redirect_bits_attribute_branchType(io_toBpu_redirect_bits_attribute_branchType),
        .io_toBpu_redirect_bits_attribute_rasAction(io_toBpu_redirect_bits_attribute_rasAction),
        .io_toBpu_redirect_bits_meta_phr_phrPtr_flag(io_toBpu_redirect_bits_meta_phr_phrPtr_flag),
        .io_toBpu_redirect_bits_meta_phr_phrPtr_value(io_toBpu_redirect_bits_meta_phr_phrPtr_value),
        .io_toBpu_redirect_bits_meta_phr_phrLowBits(io_toBpu_redirect_bits_meta_phr_phrLowBits),
        .io_toBpu_redirect_bits_meta_commonHRMeta_ghr(io_toBpu_redirect_bits_meta_commonHRMeta_ghr),
        .io_toBpu_redirect_bits_meta_commonHRMeta_bw(io_toBpu_redirect_bits_meta_commonHRMeta_bw),
        .io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_0(io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_0),
        .io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_1(io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_1),
        .io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_2(io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_2),
        .io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_3(io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_3),
        .io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_4(io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_4),
        .io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_5(io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_5),
        .io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_6(io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_6),
        .io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_7(io_toBpu_redirect_bits_meta_commonHRMeta_hitMask_7),
        .io_toBpu_redirect_bits_meta_commonHRMeta_attribute_0_branchType(io_toBpu_redirect_bits_meta_commonHRMeta_attribute_0_branchType),
        .io_toBpu_redirect_bits_meta_commonHRMeta_attribute_1_branchType(io_toBpu_redirect_bits_meta_commonHRMeta_attribute_1_branchType),
        .io_toBpu_redirect_bits_meta_commonHRMeta_attribute_2_branchType(io_toBpu_redirect_bits_meta_commonHRMeta_attribute_2_branchType),
        .io_toBpu_redirect_bits_meta_commonHRMeta_attribute_3_branchType(io_toBpu_redirect_bits_meta_commonHRMeta_attribute_3_branchType),
        .io_toBpu_redirect_bits_meta_commonHRMeta_attribute_4_branchType(io_toBpu_redirect_bits_meta_commonHRMeta_attribute_4_branchType),
        .io_toBpu_redirect_bits_meta_commonHRMeta_attribute_5_branchType(io_toBpu_redirect_bits_meta_commonHRMeta_attribute_5_branchType),
        .io_toBpu_redirect_bits_meta_commonHRMeta_attribute_6_branchType(io_toBpu_redirect_bits_meta_commonHRMeta_attribute_6_branchType),
        .io_toBpu_redirect_bits_meta_commonHRMeta_attribute_7_branchType(io_toBpu_redirect_bits_meta_commonHRMeta_attribute_7_branchType),
        .io_toBpu_redirect_bits_meta_commonHRMeta_position_0(io_toBpu_redirect_bits_meta_commonHRMeta_position_0),
        .io_toBpu_redirect_bits_meta_commonHRMeta_position_1(io_toBpu_redirect_bits_meta_commonHRMeta_position_1),
        .io_toBpu_redirect_bits_meta_commonHRMeta_position_2(io_toBpu_redirect_bits_meta_commonHRMeta_position_2),
        .io_toBpu_redirect_bits_meta_commonHRMeta_position_3(io_toBpu_redirect_bits_meta_commonHRMeta_position_3),
        .io_toBpu_redirect_bits_meta_commonHRMeta_position_4(io_toBpu_redirect_bits_meta_commonHRMeta_position_4),
        .io_toBpu_redirect_bits_meta_commonHRMeta_position_5(io_toBpu_redirect_bits_meta_commonHRMeta_position_5),
        .io_toBpu_redirect_bits_meta_commonHRMeta_position_6(io_toBpu_redirect_bits_meta_commonHRMeta_position_6),
        .io_toBpu_redirect_bits_meta_commonHRMeta_position_7(io_toBpu_redirect_bits_meta_commonHRMeta_position_7),
        .io_toBpu_redirect_bits_meta_ras_ssp(io_toBpu_redirect_bits_meta_ras_ssp),
        .io_toBpu_redirect_bits_meta_ras_sctr(io_toBpu_redirect_bits_meta_ras_sctr),
        .io_toBpu_redirect_bits_meta_ras_tosw_flag(io_toBpu_redirect_bits_meta_ras_tosw_flag),
        .io_toBpu_redirect_bits_meta_ras_tosw_value(io_toBpu_redirect_bits_meta_ras_tosw_value),
        .io_toBpu_redirect_bits_meta_ras_tosr_flag(io_toBpu_redirect_bits_meta_ras_tosr_flag),
        .io_toBpu_redirect_bits_meta_ras_tosr_value(io_toBpu_redirect_bits_meta_ras_tosr_value),
        .io_toBpu_redirect_bits_meta_ras_nos_flag(io_toBpu_redirect_bits_meta_ras_nos_flag),
        .io_toBpu_redirect_bits_meta_ras_nos_value(io_toBpu_redirect_bits_meta_ras_nos_value),
        .io_toBpu_train_valid(io_toBpu_train_valid),
        .io_toBpu_train_bits_startPc_addr(io_toBpu_train_bits_startPc_addr),
        .io_toBpu_train_bits_branches_0_valid(io_toBpu_train_bits_branches_0_valid),
        .io_toBpu_train_bits_branches_0_bits_target_addr(io_toBpu_train_bits_branches_0_bits_target_addr),
        .io_toBpu_train_bits_branches_0_bits_taken(io_toBpu_train_bits_branches_0_bits_taken),
        .io_toBpu_train_bits_branches_0_bits_cfiPosition(io_toBpu_train_bits_branches_0_bits_cfiPosition),
        .io_toBpu_train_bits_branches_0_bits_attribute_branchType(io_toBpu_train_bits_branches_0_bits_attribute_branchType),
        .io_toBpu_train_bits_branches_0_bits_attribute_rasAction(io_toBpu_train_bits_branches_0_bits_attribute_rasAction),
        .io_toBpu_train_bits_branches_0_bits_mispredict(io_toBpu_train_bits_branches_0_bits_mispredict),
        .io_toBpu_train_bits_branches_1_valid(io_toBpu_train_bits_branches_1_valid),
        .io_toBpu_train_bits_branches_1_bits_target_addr(io_toBpu_train_bits_branches_1_bits_target_addr),
        .io_toBpu_train_bits_branches_1_bits_taken(io_toBpu_train_bits_branches_1_bits_taken),
        .io_toBpu_train_bits_branches_1_bits_cfiPosition(io_toBpu_train_bits_branches_1_bits_cfiPosition),
        .io_toBpu_train_bits_branches_1_bits_attribute_branchType(io_toBpu_train_bits_branches_1_bits_attribute_branchType),
        .io_toBpu_train_bits_branches_1_bits_attribute_rasAction(io_toBpu_train_bits_branches_1_bits_attribute_rasAction),
        .io_toBpu_train_bits_branches_1_bits_mispredict(io_toBpu_train_bits_branches_1_bits_mispredict),
        .io_toBpu_train_bits_branches_2_valid(io_toBpu_train_bits_branches_2_valid),
        .io_toBpu_train_bits_branches_2_bits_target_addr(io_toBpu_train_bits_branches_2_bits_target_addr),
        .io_toBpu_train_bits_branches_2_bits_taken(io_toBpu_train_bits_branches_2_bits_taken),
        .io_toBpu_train_bits_branches_2_bits_cfiPosition(io_toBpu_train_bits_branches_2_bits_cfiPosition),
        .io_toBpu_train_bits_branches_2_bits_attribute_branchType(io_toBpu_train_bits_branches_2_bits_attribute_branchType),
        .io_toBpu_train_bits_branches_2_bits_attribute_rasAction(io_toBpu_train_bits_branches_2_bits_attribute_rasAction),
        .io_toBpu_train_bits_branches_2_bits_mispredict(io_toBpu_train_bits_branches_2_bits_mispredict),
        .io_toBpu_train_bits_branches_3_valid(io_toBpu_train_bits_branches_3_valid),
        .io_toBpu_train_bits_branches_3_bits_target_addr(io_toBpu_train_bits_branches_3_bits_target_addr),
        .io_toBpu_train_bits_branches_3_bits_taken(io_toBpu_train_bits_branches_3_bits_taken),
        .io_toBpu_train_bits_branches_3_bits_cfiPosition(io_toBpu_train_bits_branches_3_bits_cfiPosition),
        .io_toBpu_train_bits_branches_3_bits_attribute_branchType(io_toBpu_train_bits_branches_3_bits_attribute_branchType),
        .io_toBpu_train_bits_branches_3_bits_attribute_rasAction(io_toBpu_train_bits_branches_3_bits_attribute_rasAction),
        .io_toBpu_train_bits_branches_3_bits_mispredict(io_toBpu_train_bits_branches_3_bits_mispredict),
        .io_toBpu_train_bits_branches_4_valid(io_toBpu_train_bits_branches_4_valid),
        .io_toBpu_train_bits_branches_4_bits_target_addr(io_toBpu_train_bits_branches_4_bits_target_addr),
        .io_toBpu_train_bits_branches_4_bits_taken(io_toBpu_train_bits_branches_4_bits_taken),
        .io_toBpu_train_bits_branches_4_bits_cfiPosition(io_toBpu_train_bits_branches_4_bits_cfiPosition),
        .io_toBpu_train_bits_branches_4_bits_attribute_branchType(io_toBpu_train_bits_branches_4_bits_attribute_branchType),
        .io_toBpu_train_bits_branches_4_bits_attribute_rasAction(io_toBpu_train_bits_branches_4_bits_attribute_rasAction),
        .io_toBpu_train_bits_branches_4_bits_mispredict(io_toBpu_train_bits_branches_4_bits_mispredict),
        .io_toBpu_train_bits_branches_5_valid(io_toBpu_train_bits_branches_5_valid),
        .io_toBpu_train_bits_branches_5_bits_target_addr(io_toBpu_train_bits_branches_5_bits_target_addr),
        .io_toBpu_train_bits_branches_5_bits_taken(io_toBpu_train_bits_branches_5_bits_taken),
        .io_toBpu_train_bits_branches_5_bits_cfiPosition(io_toBpu_train_bits_branches_5_bits_cfiPosition),
        .io_toBpu_train_bits_branches_5_bits_attribute_branchType(io_toBpu_train_bits_branches_5_bits_attribute_branchType),
        .io_toBpu_train_bits_branches_5_bits_attribute_rasAction(io_toBpu_train_bits_branches_5_bits_attribute_rasAction),
        .io_toBpu_train_bits_branches_5_bits_mispredict(io_toBpu_train_bits_branches_5_bits_mispredict),
        .io_toBpu_train_bits_branches_6_valid(io_toBpu_train_bits_branches_6_valid),
        .io_toBpu_train_bits_branches_6_bits_target_addr(io_toBpu_train_bits_branches_6_bits_target_addr),
        .io_toBpu_train_bits_branches_6_bits_taken(io_toBpu_train_bits_branches_6_bits_taken),
        .io_toBpu_train_bits_branches_6_bits_cfiPosition(io_toBpu_train_bits_branches_6_bits_cfiPosition),
        .io_toBpu_train_bits_branches_6_bits_attribute_branchType(io_toBpu_train_bits_branches_6_bits_attribute_branchType),
        .io_toBpu_train_bits_branches_6_bits_attribute_rasAction(io_toBpu_train_bits_branches_6_bits_attribute_rasAction),
        .io_toBpu_train_bits_branches_6_bits_mispredict(io_toBpu_train_bits_branches_6_bits_mispredict),
        .io_toBpu_train_bits_branches_7_valid(io_toBpu_train_bits_branches_7_valid),
        .io_toBpu_train_bits_branches_7_bits_target_addr(io_toBpu_train_bits_branches_7_bits_target_addr),
        .io_toBpu_train_bits_branches_7_bits_taken(io_toBpu_train_bits_branches_7_bits_taken),
        .io_toBpu_train_bits_branches_7_bits_cfiPosition(io_toBpu_train_bits_branches_7_bits_cfiPosition),
        .io_toBpu_train_bits_branches_7_bits_attribute_branchType(io_toBpu_train_bits_branches_7_bits_attribute_branchType),
        .io_toBpu_train_bits_branches_7_bits_attribute_rasAction(io_toBpu_train_bits_branches_7_bits_attribute_rasAction),
        .io_toBpu_train_bits_branches_7_bits_mispredict(io_toBpu_train_bits_branches_7_bits_mispredict),
        .io_toBpu_train_bits_meta_mbtb_entries_0_0_rawHit(io_toBpu_train_bits_meta_mbtb_entries_0_0_rawHit),
        .io_toBpu_train_bits_meta_mbtb_entries_0_0_position(io_toBpu_train_bits_meta_mbtb_entries_0_0_position),
        .io_toBpu_train_bits_meta_mbtb_entries_0_0_attribute_branchType(io_toBpu_train_bits_meta_mbtb_entries_0_0_attribute_branchType),
        .io_toBpu_train_bits_meta_mbtb_entries_0_0_attribute_rasAction(io_toBpu_train_bits_meta_mbtb_entries_0_0_attribute_rasAction),
        .io_toBpu_train_bits_meta_mbtb_entries_0_0_counter_value(io_toBpu_train_bits_meta_mbtb_entries_0_0_counter_value),
        .io_toBpu_train_bits_meta_mbtb_entries_0_1_rawHit(io_toBpu_train_bits_meta_mbtb_entries_0_1_rawHit),
        .io_toBpu_train_bits_meta_mbtb_entries_0_1_position(io_toBpu_train_bits_meta_mbtb_entries_0_1_position),
        .io_toBpu_train_bits_meta_mbtb_entries_0_1_attribute_branchType(io_toBpu_train_bits_meta_mbtb_entries_0_1_attribute_branchType),
        .io_toBpu_train_bits_meta_mbtb_entries_0_1_attribute_rasAction(io_toBpu_train_bits_meta_mbtb_entries_0_1_attribute_rasAction),
        .io_toBpu_train_bits_meta_mbtb_entries_0_1_counter_value(io_toBpu_train_bits_meta_mbtb_entries_0_1_counter_value),
        .io_toBpu_train_bits_meta_mbtb_entries_0_2_rawHit(io_toBpu_train_bits_meta_mbtb_entries_0_2_rawHit),
        .io_toBpu_train_bits_meta_mbtb_entries_0_2_position(io_toBpu_train_bits_meta_mbtb_entries_0_2_position),
        .io_toBpu_train_bits_meta_mbtb_entries_0_2_attribute_branchType(io_toBpu_train_bits_meta_mbtb_entries_0_2_attribute_branchType),
        .io_toBpu_train_bits_meta_mbtb_entries_0_2_attribute_rasAction(io_toBpu_train_bits_meta_mbtb_entries_0_2_attribute_rasAction),
        .io_toBpu_train_bits_meta_mbtb_entries_0_2_counter_value(io_toBpu_train_bits_meta_mbtb_entries_0_2_counter_value),
        .io_toBpu_train_bits_meta_mbtb_entries_0_3_rawHit(io_toBpu_train_bits_meta_mbtb_entries_0_3_rawHit),
        .io_toBpu_train_bits_meta_mbtb_entries_0_3_position(io_toBpu_train_bits_meta_mbtb_entries_0_3_position),
        .io_toBpu_train_bits_meta_mbtb_entries_0_3_attribute_branchType(io_toBpu_train_bits_meta_mbtb_entries_0_3_attribute_branchType),
        .io_toBpu_train_bits_meta_mbtb_entries_0_3_attribute_rasAction(io_toBpu_train_bits_meta_mbtb_entries_0_3_attribute_rasAction),
        .io_toBpu_train_bits_meta_mbtb_entries_0_3_counter_value(io_toBpu_train_bits_meta_mbtb_entries_0_3_counter_value),
        .io_toBpu_train_bits_meta_mbtb_entries_1_0_rawHit(io_toBpu_train_bits_meta_mbtb_entries_1_0_rawHit),
        .io_toBpu_train_bits_meta_mbtb_entries_1_0_position(io_toBpu_train_bits_meta_mbtb_entries_1_0_position),
        .io_toBpu_train_bits_meta_mbtb_entries_1_0_attribute_branchType(io_toBpu_train_bits_meta_mbtb_entries_1_0_attribute_branchType),
        .io_toBpu_train_bits_meta_mbtb_entries_1_0_attribute_rasAction(io_toBpu_train_bits_meta_mbtb_entries_1_0_attribute_rasAction),
        .io_toBpu_train_bits_meta_mbtb_entries_1_0_counter_value(io_toBpu_train_bits_meta_mbtb_entries_1_0_counter_value),
        .io_toBpu_train_bits_meta_mbtb_entries_1_1_rawHit(io_toBpu_train_bits_meta_mbtb_entries_1_1_rawHit),
        .io_toBpu_train_bits_meta_mbtb_entries_1_1_position(io_toBpu_train_bits_meta_mbtb_entries_1_1_position),
        .io_toBpu_train_bits_meta_mbtb_entries_1_1_attribute_branchType(io_toBpu_train_bits_meta_mbtb_entries_1_1_attribute_branchType),
        .io_toBpu_train_bits_meta_mbtb_entries_1_1_attribute_rasAction(io_toBpu_train_bits_meta_mbtb_entries_1_1_attribute_rasAction),
        .io_toBpu_train_bits_meta_mbtb_entries_1_1_counter_value(io_toBpu_train_bits_meta_mbtb_entries_1_1_counter_value),
        .io_toBpu_train_bits_meta_mbtb_entries_1_2_rawHit(io_toBpu_train_bits_meta_mbtb_entries_1_2_rawHit),
        .io_toBpu_train_bits_meta_mbtb_entries_1_2_position(io_toBpu_train_bits_meta_mbtb_entries_1_2_position),
        .io_toBpu_train_bits_meta_mbtb_entries_1_2_attribute_branchType(io_toBpu_train_bits_meta_mbtb_entries_1_2_attribute_branchType),
        .io_toBpu_train_bits_meta_mbtb_entries_1_2_attribute_rasAction(io_toBpu_train_bits_meta_mbtb_entries_1_2_attribute_rasAction),
        .io_toBpu_train_bits_meta_mbtb_entries_1_2_counter_value(io_toBpu_train_bits_meta_mbtb_entries_1_2_counter_value),
        .io_toBpu_train_bits_meta_mbtb_entries_1_3_rawHit(io_toBpu_train_bits_meta_mbtb_entries_1_3_rawHit),
        .io_toBpu_train_bits_meta_mbtb_entries_1_3_position(io_toBpu_train_bits_meta_mbtb_entries_1_3_position),
        .io_toBpu_train_bits_meta_mbtb_entries_1_3_attribute_branchType(io_toBpu_train_bits_meta_mbtb_entries_1_3_attribute_branchType),
        .io_toBpu_train_bits_meta_mbtb_entries_1_3_attribute_rasAction(io_toBpu_train_bits_meta_mbtb_entries_1_3_attribute_rasAction),
        .io_toBpu_train_bits_meta_mbtb_entries_1_3_counter_value(io_toBpu_train_bits_meta_mbtb_entries_1_3_counter_value),
        .io_toBpu_train_bits_meta_tage_entries_0_useProvider(io_toBpu_train_bits_meta_tage_entries_0_useProvider),
        .io_toBpu_train_bits_meta_tage_entries_0_providerTableIdx(io_toBpu_train_bits_meta_tage_entries_0_providerTableIdx),
        .io_toBpu_train_bits_meta_tage_entries_0_providerWayIdx(io_toBpu_train_bits_meta_tage_entries_0_providerWayIdx),
        .io_toBpu_train_bits_meta_tage_entries_0_providerTakenCtr_value(io_toBpu_train_bits_meta_tage_entries_0_providerTakenCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_0_providerUsefulCtr_value(io_toBpu_train_bits_meta_tage_entries_0_providerUsefulCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_0_altOrBasePred(io_toBpu_train_bits_meta_tage_entries_0_altOrBasePred),
        .io_toBpu_train_bits_meta_tage_entries_1_useProvider(io_toBpu_train_bits_meta_tage_entries_1_useProvider),
        .io_toBpu_train_bits_meta_tage_entries_1_providerTableIdx(io_toBpu_train_bits_meta_tage_entries_1_providerTableIdx),
        .io_toBpu_train_bits_meta_tage_entries_1_providerWayIdx(io_toBpu_train_bits_meta_tage_entries_1_providerWayIdx),
        .io_toBpu_train_bits_meta_tage_entries_1_providerTakenCtr_value(io_toBpu_train_bits_meta_tage_entries_1_providerTakenCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_1_providerUsefulCtr_value(io_toBpu_train_bits_meta_tage_entries_1_providerUsefulCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_1_altOrBasePred(io_toBpu_train_bits_meta_tage_entries_1_altOrBasePred),
        .io_toBpu_train_bits_meta_tage_entries_2_useProvider(io_toBpu_train_bits_meta_tage_entries_2_useProvider),
        .io_toBpu_train_bits_meta_tage_entries_2_providerTableIdx(io_toBpu_train_bits_meta_tage_entries_2_providerTableIdx),
        .io_toBpu_train_bits_meta_tage_entries_2_providerWayIdx(io_toBpu_train_bits_meta_tage_entries_2_providerWayIdx),
        .io_toBpu_train_bits_meta_tage_entries_2_providerTakenCtr_value(io_toBpu_train_bits_meta_tage_entries_2_providerTakenCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_2_providerUsefulCtr_value(io_toBpu_train_bits_meta_tage_entries_2_providerUsefulCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_2_altOrBasePred(io_toBpu_train_bits_meta_tage_entries_2_altOrBasePred),
        .io_toBpu_train_bits_meta_tage_entries_3_useProvider(io_toBpu_train_bits_meta_tage_entries_3_useProvider),
        .io_toBpu_train_bits_meta_tage_entries_3_providerTableIdx(io_toBpu_train_bits_meta_tage_entries_3_providerTableIdx),
        .io_toBpu_train_bits_meta_tage_entries_3_providerWayIdx(io_toBpu_train_bits_meta_tage_entries_3_providerWayIdx),
        .io_toBpu_train_bits_meta_tage_entries_3_providerTakenCtr_value(io_toBpu_train_bits_meta_tage_entries_3_providerTakenCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_3_providerUsefulCtr_value(io_toBpu_train_bits_meta_tage_entries_3_providerUsefulCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_3_altOrBasePred(io_toBpu_train_bits_meta_tage_entries_3_altOrBasePred),
        .io_toBpu_train_bits_meta_tage_entries_4_useProvider(io_toBpu_train_bits_meta_tage_entries_4_useProvider),
        .io_toBpu_train_bits_meta_tage_entries_4_providerTableIdx(io_toBpu_train_bits_meta_tage_entries_4_providerTableIdx),
        .io_toBpu_train_bits_meta_tage_entries_4_providerWayIdx(io_toBpu_train_bits_meta_tage_entries_4_providerWayIdx),
        .io_toBpu_train_bits_meta_tage_entries_4_providerTakenCtr_value(io_toBpu_train_bits_meta_tage_entries_4_providerTakenCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_4_providerUsefulCtr_value(io_toBpu_train_bits_meta_tage_entries_4_providerUsefulCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_4_altOrBasePred(io_toBpu_train_bits_meta_tage_entries_4_altOrBasePred),
        .io_toBpu_train_bits_meta_tage_entries_5_useProvider(io_toBpu_train_bits_meta_tage_entries_5_useProvider),
        .io_toBpu_train_bits_meta_tage_entries_5_providerTableIdx(io_toBpu_train_bits_meta_tage_entries_5_providerTableIdx),
        .io_toBpu_train_bits_meta_tage_entries_5_providerWayIdx(io_toBpu_train_bits_meta_tage_entries_5_providerWayIdx),
        .io_toBpu_train_bits_meta_tage_entries_5_providerTakenCtr_value(io_toBpu_train_bits_meta_tage_entries_5_providerTakenCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_5_providerUsefulCtr_value(io_toBpu_train_bits_meta_tage_entries_5_providerUsefulCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_5_altOrBasePred(io_toBpu_train_bits_meta_tage_entries_5_altOrBasePred),
        .io_toBpu_train_bits_meta_tage_entries_6_useProvider(io_toBpu_train_bits_meta_tage_entries_6_useProvider),
        .io_toBpu_train_bits_meta_tage_entries_6_providerTableIdx(io_toBpu_train_bits_meta_tage_entries_6_providerTableIdx),
        .io_toBpu_train_bits_meta_tage_entries_6_providerWayIdx(io_toBpu_train_bits_meta_tage_entries_6_providerWayIdx),
        .io_toBpu_train_bits_meta_tage_entries_6_providerTakenCtr_value(io_toBpu_train_bits_meta_tage_entries_6_providerTakenCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_6_providerUsefulCtr_value(io_toBpu_train_bits_meta_tage_entries_6_providerUsefulCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_6_altOrBasePred(io_toBpu_train_bits_meta_tage_entries_6_altOrBasePred),
        .io_toBpu_train_bits_meta_tage_entries_7_useProvider(io_toBpu_train_bits_meta_tage_entries_7_useProvider),
        .io_toBpu_train_bits_meta_tage_entries_7_providerTableIdx(io_toBpu_train_bits_meta_tage_entries_7_providerTableIdx),
        .io_toBpu_train_bits_meta_tage_entries_7_providerWayIdx(io_toBpu_train_bits_meta_tage_entries_7_providerWayIdx),
        .io_toBpu_train_bits_meta_tage_entries_7_providerTakenCtr_value(io_toBpu_train_bits_meta_tage_entries_7_providerTakenCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_7_providerUsefulCtr_value(io_toBpu_train_bits_meta_tage_entries_7_providerUsefulCtr_value),
        .io_toBpu_train_bits_meta_tage_entries_7_altOrBasePred(io_toBpu_train_bits_meta_tage_entries_7_altOrBasePred),
        .io_toBpu_train_bits_meta_sc_scPathResp_0_0(io_toBpu_train_bits_meta_sc_scPathResp_0_0),
        .io_toBpu_train_bits_meta_sc_scPathResp_0_1(io_toBpu_train_bits_meta_sc_scPathResp_0_1),
        .io_toBpu_train_bits_meta_sc_scPathResp_0_2(io_toBpu_train_bits_meta_sc_scPathResp_0_2),
        .io_toBpu_train_bits_meta_sc_scPathResp_0_3(io_toBpu_train_bits_meta_sc_scPathResp_0_3),
        .io_toBpu_train_bits_meta_sc_scPathResp_0_4(io_toBpu_train_bits_meta_sc_scPathResp_0_4),
        .io_toBpu_train_bits_meta_sc_scPathResp_0_5(io_toBpu_train_bits_meta_sc_scPathResp_0_5),
        .io_toBpu_train_bits_meta_sc_scPathResp_0_6(io_toBpu_train_bits_meta_sc_scPathResp_0_6),
        .io_toBpu_train_bits_meta_sc_scPathResp_0_7(io_toBpu_train_bits_meta_sc_scPathResp_0_7),
        .io_toBpu_train_bits_meta_sc_scPathResp_1_0(io_toBpu_train_bits_meta_sc_scPathResp_1_0),
        .io_toBpu_train_bits_meta_sc_scPathResp_1_1(io_toBpu_train_bits_meta_sc_scPathResp_1_1),
        .io_toBpu_train_bits_meta_sc_scPathResp_1_2(io_toBpu_train_bits_meta_sc_scPathResp_1_2),
        .io_toBpu_train_bits_meta_sc_scPathResp_1_3(io_toBpu_train_bits_meta_sc_scPathResp_1_3),
        .io_toBpu_train_bits_meta_sc_scPathResp_1_4(io_toBpu_train_bits_meta_sc_scPathResp_1_4),
        .io_toBpu_train_bits_meta_sc_scPathResp_1_5(io_toBpu_train_bits_meta_sc_scPathResp_1_5),
        .io_toBpu_train_bits_meta_sc_scPathResp_1_6(io_toBpu_train_bits_meta_sc_scPathResp_1_6),
        .io_toBpu_train_bits_meta_sc_scPathResp_1_7(io_toBpu_train_bits_meta_sc_scPathResp_1_7),
        .io_toBpu_train_bits_meta_sc_scBiasResp_0(io_toBpu_train_bits_meta_sc_scBiasResp_0),
        .io_toBpu_train_bits_meta_sc_scBiasResp_1(io_toBpu_train_bits_meta_sc_scBiasResp_1),
        .io_toBpu_train_bits_meta_sc_scBiasResp_2(io_toBpu_train_bits_meta_sc_scBiasResp_2),
        .io_toBpu_train_bits_meta_sc_scBiasResp_3(io_toBpu_train_bits_meta_sc_scBiasResp_3),
        .io_toBpu_train_bits_meta_sc_scBiasResp_4(io_toBpu_train_bits_meta_sc_scBiasResp_4),
        .io_toBpu_train_bits_meta_sc_scBiasResp_5(io_toBpu_train_bits_meta_sc_scBiasResp_5),
        .io_toBpu_train_bits_meta_sc_scBiasResp_6(io_toBpu_train_bits_meta_sc_scBiasResp_6),
        .io_toBpu_train_bits_meta_sc_scBiasResp_7(io_toBpu_train_bits_meta_sc_scBiasResp_7),
        .io_toBpu_train_bits_meta_sc_scBiasResp_8(io_toBpu_train_bits_meta_sc_scBiasResp_8),
        .io_toBpu_train_bits_meta_sc_scBiasResp_9(io_toBpu_train_bits_meta_sc_scBiasResp_9),
        .io_toBpu_train_bits_meta_sc_scBiasResp_10(io_toBpu_train_bits_meta_sc_scBiasResp_10),
        .io_toBpu_train_bits_meta_sc_scBiasResp_11(io_toBpu_train_bits_meta_sc_scBiasResp_11),
        .io_toBpu_train_bits_meta_sc_scBiasResp_12(io_toBpu_train_bits_meta_sc_scBiasResp_12),
        .io_toBpu_train_bits_meta_sc_scBiasResp_13(io_toBpu_train_bits_meta_sc_scBiasResp_13),
        .io_toBpu_train_bits_meta_sc_scBiasResp_14(io_toBpu_train_bits_meta_sc_scBiasResp_14),
        .io_toBpu_train_bits_meta_sc_scBiasResp_15(io_toBpu_train_bits_meta_sc_scBiasResp_15),
        .io_toBpu_train_bits_meta_sc_scBiasResp_16(io_toBpu_train_bits_meta_sc_scBiasResp_16),
        .io_toBpu_train_bits_meta_sc_scBiasResp_17(io_toBpu_train_bits_meta_sc_scBiasResp_17),
        .io_toBpu_train_bits_meta_sc_scBiasResp_18(io_toBpu_train_bits_meta_sc_scBiasResp_18),
        .io_toBpu_train_bits_meta_sc_scBiasResp_19(io_toBpu_train_bits_meta_sc_scBiasResp_19),
        .io_toBpu_train_bits_meta_sc_scBiasResp_20(io_toBpu_train_bits_meta_sc_scBiasResp_20),
        .io_toBpu_train_bits_meta_sc_scBiasResp_21(io_toBpu_train_bits_meta_sc_scBiasResp_21),
        .io_toBpu_train_bits_meta_sc_scBiasResp_22(io_toBpu_train_bits_meta_sc_scBiasResp_22),
        .io_toBpu_train_bits_meta_sc_scBiasResp_23(io_toBpu_train_bits_meta_sc_scBiasResp_23),
        .io_toBpu_train_bits_meta_sc_scBiasResp_24(io_toBpu_train_bits_meta_sc_scBiasResp_24),
        .io_toBpu_train_bits_meta_sc_scBiasResp_25(io_toBpu_train_bits_meta_sc_scBiasResp_25),
        .io_toBpu_train_bits_meta_sc_scBiasResp_26(io_toBpu_train_bits_meta_sc_scBiasResp_26),
        .io_toBpu_train_bits_meta_sc_scBiasResp_27(io_toBpu_train_bits_meta_sc_scBiasResp_27),
        .io_toBpu_train_bits_meta_sc_scBiasResp_28(io_toBpu_train_bits_meta_sc_scBiasResp_28),
        .io_toBpu_train_bits_meta_sc_scBiasResp_29(io_toBpu_train_bits_meta_sc_scBiasResp_29),
        .io_toBpu_train_bits_meta_sc_scBiasResp_30(io_toBpu_train_bits_meta_sc_scBiasResp_30),
        .io_toBpu_train_bits_meta_sc_scBiasResp_31(io_toBpu_train_bits_meta_sc_scBiasResp_31),
        .io_toBpu_train_bits_meta_sc_scBiasLowerBits_0(io_toBpu_train_bits_meta_sc_scBiasLowerBits_0),
        .io_toBpu_train_bits_meta_sc_scBiasLowerBits_1(io_toBpu_train_bits_meta_sc_scBiasLowerBits_1),
        .io_toBpu_train_bits_meta_sc_scBiasLowerBits_2(io_toBpu_train_bits_meta_sc_scBiasLowerBits_2),
        .io_toBpu_train_bits_meta_sc_scBiasLowerBits_3(io_toBpu_train_bits_meta_sc_scBiasLowerBits_3),
        .io_toBpu_train_bits_meta_sc_scBiasLowerBits_4(io_toBpu_train_bits_meta_sc_scBiasLowerBits_4),
        .io_toBpu_train_bits_meta_sc_scBiasLowerBits_5(io_toBpu_train_bits_meta_sc_scBiasLowerBits_5),
        .io_toBpu_train_bits_meta_sc_scBiasLowerBits_6(io_toBpu_train_bits_meta_sc_scBiasLowerBits_6),
        .io_toBpu_train_bits_meta_sc_scBiasLowerBits_7(io_toBpu_train_bits_meta_sc_scBiasLowerBits_7),
        .io_toBpu_train_bits_meta_sc_scCommonHR_valid(io_toBpu_train_bits_meta_sc_scCommonHR_valid),
        .io_toBpu_train_bits_meta_sc_scCommonHR_ghr(io_toBpu_train_bits_meta_sc_scCommonHR_ghr),
        .io_toBpu_train_bits_meta_sc_scCommonHR_bw(io_toBpu_train_bits_meta_sc_scCommonHR_bw),
        .io_toBpu_train_bits_meta_sc_scPred_0(io_toBpu_train_bits_meta_sc_scPred_0),
        .io_toBpu_train_bits_meta_sc_scPred_1(io_toBpu_train_bits_meta_sc_scPred_1),
        .io_toBpu_train_bits_meta_sc_scPred_2(io_toBpu_train_bits_meta_sc_scPred_2),
        .io_toBpu_train_bits_meta_sc_scPred_3(io_toBpu_train_bits_meta_sc_scPred_3),
        .io_toBpu_train_bits_meta_sc_scPred_4(io_toBpu_train_bits_meta_sc_scPred_4),
        .io_toBpu_train_bits_meta_sc_scPred_5(io_toBpu_train_bits_meta_sc_scPred_5),
        .io_toBpu_train_bits_meta_sc_scPred_6(io_toBpu_train_bits_meta_sc_scPred_6),
        .io_toBpu_train_bits_meta_sc_scPred_7(io_toBpu_train_bits_meta_sc_scPred_7),
        .io_toBpu_train_bits_meta_sc_tagePred_0(io_toBpu_train_bits_meta_sc_tagePred_0),
        .io_toBpu_train_bits_meta_sc_tagePred_1(io_toBpu_train_bits_meta_sc_tagePred_1),
        .io_toBpu_train_bits_meta_sc_tagePred_2(io_toBpu_train_bits_meta_sc_tagePred_2),
        .io_toBpu_train_bits_meta_sc_tagePred_3(io_toBpu_train_bits_meta_sc_tagePred_3),
        .io_toBpu_train_bits_meta_sc_tagePred_4(io_toBpu_train_bits_meta_sc_tagePred_4),
        .io_toBpu_train_bits_meta_sc_tagePred_5(io_toBpu_train_bits_meta_sc_tagePred_5),
        .io_toBpu_train_bits_meta_sc_tagePred_6(io_toBpu_train_bits_meta_sc_tagePred_6),
        .io_toBpu_train_bits_meta_sc_tagePred_7(io_toBpu_train_bits_meta_sc_tagePred_7),
        .io_toBpu_train_bits_meta_sc_tagePredValid_0(io_toBpu_train_bits_meta_sc_tagePredValid_0),
        .io_toBpu_train_bits_meta_sc_tagePredValid_1(io_toBpu_train_bits_meta_sc_tagePredValid_1),
        .io_toBpu_train_bits_meta_sc_tagePredValid_2(io_toBpu_train_bits_meta_sc_tagePredValid_2),
        .io_toBpu_train_bits_meta_sc_tagePredValid_3(io_toBpu_train_bits_meta_sc_tagePredValid_3),
        .io_toBpu_train_bits_meta_sc_tagePredValid_4(io_toBpu_train_bits_meta_sc_tagePredValid_4),
        .io_toBpu_train_bits_meta_sc_tagePredValid_5(io_toBpu_train_bits_meta_sc_tagePredValid_5),
        .io_toBpu_train_bits_meta_sc_tagePredValid_6(io_toBpu_train_bits_meta_sc_tagePredValid_6),
        .io_toBpu_train_bits_meta_sc_tagePredValid_7(io_toBpu_train_bits_meta_sc_tagePredValid_7),
        .io_toBpu_train_bits_meta_sc_useScPred_0(io_toBpu_train_bits_meta_sc_useScPred_0),
        .io_toBpu_train_bits_meta_sc_useScPred_1(io_toBpu_train_bits_meta_sc_useScPred_1),
        .io_toBpu_train_bits_meta_sc_useScPred_2(io_toBpu_train_bits_meta_sc_useScPred_2),
        .io_toBpu_train_bits_meta_sc_useScPred_3(io_toBpu_train_bits_meta_sc_useScPred_3),
        .io_toBpu_train_bits_meta_sc_useScPred_4(io_toBpu_train_bits_meta_sc_useScPred_4),
        .io_toBpu_train_bits_meta_sc_useScPred_5(io_toBpu_train_bits_meta_sc_useScPred_5),
        .io_toBpu_train_bits_meta_sc_useScPred_6(io_toBpu_train_bits_meta_sc_useScPred_6),
        .io_toBpu_train_bits_meta_sc_useScPred_7(io_toBpu_train_bits_meta_sc_useScPred_7),
        .io_toBpu_train_bits_meta_sc_sumAboveThres_0(io_toBpu_train_bits_meta_sc_sumAboveThres_0),
        .io_toBpu_train_bits_meta_sc_sumAboveThres_1(io_toBpu_train_bits_meta_sc_sumAboveThres_1),
        .io_toBpu_train_bits_meta_sc_sumAboveThres_2(io_toBpu_train_bits_meta_sc_sumAboveThres_2),
        .io_toBpu_train_bits_meta_sc_sumAboveThres_3(io_toBpu_train_bits_meta_sc_sumAboveThres_3),
        .io_toBpu_train_bits_meta_sc_sumAboveThres_4(io_toBpu_train_bits_meta_sc_sumAboveThres_4),
        .io_toBpu_train_bits_meta_sc_sumAboveThres_5(io_toBpu_train_bits_meta_sc_sumAboveThres_5),
        .io_toBpu_train_bits_meta_sc_sumAboveThres_6(io_toBpu_train_bits_meta_sc_sumAboveThres_6),
        .io_toBpu_train_bits_meta_sc_sumAboveThres_7(io_toBpu_train_bits_meta_sc_sumAboveThres_7),
        .io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_0(io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_0),
        .io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_1(io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_1),
        .io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_2(io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_2),
        .io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_3(io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_3),
        .io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_4(io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_4),
        .io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_5(io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_5),
        .io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_6(io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_6),
        .io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_7(io_toBpu_train_bits_meta_sc_debug_scPathTakenVec_7),
        .io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_0(io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_0),
        .io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_1(io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_1),
        .io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_2(io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_2),
        .io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_3(io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_3),
        .io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_4(io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_4),
        .io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_5(io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_5),
        .io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_6(io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_6),
        .io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_7(io_toBpu_train_bits_meta_sc_debug_scBiasTakenVec_7),
        .io_toBpu_train_bits_meta_sc_debug_predPathIdx_0(io_toBpu_train_bits_meta_sc_debug_predPathIdx_0),
        .io_toBpu_train_bits_meta_sc_debug_predPathIdx_1(io_toBpu_train_bits_meta_sc_debug_predPathIdx_1),
        .io_toBpu_train_bits_meta_sc_debug_predGlobalIdx_0(io_toBpu_train_bits_meta_sc_debug_predGlobalIdx_0),
        .io_toBpu_train_bits_meta_sc_debug_predGlobalIdx_1(io_toBpu_train_bits_meta_sc_debug_predGlobalIdx_1),
        .io_toBpu_train_bits_meta_sc_debug_predBWIdx_0(io_toBpu_train_bits_meta_sc_debug_predBWIdx_0),
        .io_toBpu_train_bits_meta_sc_debug_predBWIdx_1(io_toBpu_train_bits_meta_sc_debug_predBWIdx_1),
        .io_toBpu_train_bits_meta_sc_debug_predBiasIdx(io_toBpu_train_bits_meta_sc_debug_predBiasIdx),
        .io_toBpu_train_bits_meta_ittage_provider_valid(io_toBpu_train_bits_meta_ittage_provider_valid),
        .io_toBpu_train_bits_meta_ittage_provider_bits(io_toBpu_train_bits_meta_ittage_provider_bits),
        .io_toBpu_train_bits_meta_ittage_altProvider_valid(io_toBpu_train_bits_meta_ittage_altProvider_valid),
        .io_toBpu_train_bits_meta_ittage_altProvider_bits(io_toBpu_train_bits_meta_ittage_altProvider_bits),
        .io_toBpu_train_bits_meta_ittage_altDiffers(io_toBpu_train_bits_meta_ittage_altDiffers),
        .io_toBpu_train_bits_meta_ittage_providerUsefulCnt_value(io_toBpu_train_bits_meta_ittage_providerUsefulCnt_value),
        .io_toBpu_train_bits_meta_ittage_providerCnt_value(io_toBpu_train_bits_meta_ittage_providerCnt_value),
        .io_toBpu_train_bits_meta_ittage_altProviderCnt_value(io_toBpu_train_bits_meta_ittage_altProviderCnt_value),
        .io_toBpu_train_bits_meta_ittage_allocate_valid(io_toBpu_train_bits_meta_ittage_allocate_valid),
        .io_toBpu_train_bits_meta_ittage_allocate_bits(io_toBpu_train_bits_meta_ittage_allocate_bits),
        .io_toBpu_train_bits_meta_ittage_providerTarget_addr(io_toBpu_train_bits_meta_ittage_providerTarget_addr),
        .io_toBpu_train_bits_meta_ittage_altProviderTarget_addr(io_toBpu_train_bits_meta_ittage_altProviderTarget_addr),
        .io_toBpu_train_bits_meta_phr_phrPtr_value(io_toBpu_train_bits_meta_phr_phrPtr_value),
        .io_toBpu_train_bits_meta_phr_phrLowBits(io_toBpu_train_bits_meta_phr_phrLowBits),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist),
        .io_toBpu_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist(io_toBpu_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist),
        .io_toBpu_commit_valid(io_toBpu_commit_valid),
        .io_toBpu_commit_bits_meta_ras_ssp(io_toBpu_commit_bits_meta_ras_ssp),
        .io_toBpu_commit_bits_meta_ras_tosw_flag(io_toBpu_commit_bits_meta_ras_tosw_flag),
        .io_toBpu_commit_bits_meta_ras_tosw_value(io_toBpu_commit_bits_meta_ras_tosw_value),
        .io_toBpu_commit_bits_attribute_rasAction(io_toBpu_commit_bits_attribute_rasAction),
        .io_toBpu_bpuPtr_flag(io_toBpu_bpuPtr_flag),
        .io_toBpu_bpuPtr_value(io_toBpu_bpuPtr_value),
        .io_fromIfu_mmioCommitRead_mmioLastCommit(io_fromIfu_mmioCommitRead_mmioLastCommit),
        .io_toIfu_req_valid(io_toIfu_req_valid),
        .io_toIfu_req_bits_fetch_0_valid(io_toIfu_req_bits_fetch_0_valid),
        .io_toIfu_req_bits_fetch_0_startVAddr_addr(io_toIfu_req_bits_fetch_0_startVAddr_addr),
        .io_toIfu_req_bits_fetch_0_nextStartVAddr_addr(io_toIfu_req_bits_fetch_0_nextStartVAddr_addr),
        .io_toIfu_req_bits_fetch_0_ftqIdx_flag(io_toIfu_req_bits_fetch_0_ftqIdx_flag),
        .io_toIfu_req_bits_fetch_0_ftqIdx_value(io_toIfu_req_bits_fetch_0_ftqIdx_value),
        .io_toIfu_req_bits_fetch_0_takenCfiOffset_valid(io_toIfu_req_bits_fetch_0_takenCfiOffset_valid),
        .io_toIfu_req_bits_fetch_0_takenCfiOffset_bits(io_toIfu_req_bits_fetch_0_takenCfiOffset_bits),
        .io_toIfu_req_bits_topdownInfo_reasons_7(io_toIfu_req_bits_topdownInfo_reasons_7),
        .io_toIfu_redirect_valid(io_toIfu_redirect_valid),
        .io_toIfu_topdownRedirect_valid(io_toIfu_topdownRedirect_valid),
        .io_toIfu_topdownRedirect_bits_debugIsCtrl(io_toIfu_topdownRedirect_bits_debugIsCtrl),
        .io_toIfu_topdownRedirect_bits_debugIsMemVio(io_toIfu_topdownRedirect_bits_debugIsMemVio),
        .io_toIfu_flushFromBpu_s3_valid(io_toIfu_flushFromBpu_s3_valid),
        .io_toIfu_flushFromBpu_s3_bits_flag(io_toIfu_flushFromBpu_s3_bits_flag),
        .io_toIfu_flushFromBpu_s3_bits_value(io_toIfu_flushFromBpu_s3_bits_value),
        .io_toICache_fetchReq_valid(io_toICache_fetchReq_valid),
        .io_toICache_fetchReq_bits_startVAddr_addr(io_toICache_fetchReq_bits_startVAddr_addr),
        .io_toICache_fetchReq_bits_nextCachelineVAddr_addr(io_toICache_fetchReq_bits_nextCachelineVAddr_addr),
        .io_toICache_fetchReq_bits_ftqIdx_flag(io_toICache_fetchReq_bits_ftqIdx_flag),
        .io_toICache_fetchReq_bits_ftqIdx_value(io_toICache_fetchReq_bits_ftqIdx_value),
        .io_toICache_fetchReq_bits_takenCfiOffset(io_toICache_fetchReq_bits_takenCfiOffset),
        .io_toICache_fetchReq_bits_isBackendException(io_toICache_fetchReq_bits_isBackendException),
        .io_toICache_prefetchReq_valid(io_toICache_prefetchReq_valid),
        .io_toICache_prefetchReq_bits_startVAddr_addr(io_toICache_prefetchReq_bits_startVAddr_addr),
        .io_toICache_prefetchReq_bits_nextCachelineVAddr_addr(io_toICache_prefetchReq_bits_nextCachelineVAddr_addr),
        .io_toICache_prefetchReq_bits_ftqIdx_flag(io_toICache_prefetchReq_bits_ftqIdx_flag),
        .io_toICache_prefetchReq_bits_ftqIdx_value(io_toICache_prefetchReq_bits_ftqIdx_value),
        .io_toICache_prefetchReq_bits_takenCfiOffset(io_toICache_prefetchReq_bits_takenCfiOffset),
        .io_toICache_prefetchReq_bits_backendException_value(io_toICache_prefetchReq_bits_backendException_value),
        .io_toICache_flushFromBpu_s3_valid(io_toICache_flushFromBpu_s3_valid),
        .io_toICache_flushFromBpu_s3_bits_flag(io_toICache_flushFromBpu_s3_bits_flag),
        .io_toICache_flushFromBpu_s3_bits_value(io_toICache_flushFromBpu_s3_bits_value),
        .io_toICache_redirectFlush(io_toICache_redirectFlush),
        .io_toBackend_wen(io_toBackend_wen),
        .io_toBackend_ftqIdx(io_toBackend_ftqIdx),
        .io_toBackend_startPc_addr(io_toBackend_startPc_addr),
        .io_bpuTopDownInfo_tageMissBubble(io_bpuTopDownInfo_tageMissBubble),
        .io_bpuTopDownInfo_ittageMissBubble(io_bpuTopDownInfo_ittageMissBubble),
        .io_bpuTopDownInfo_rasMissBubble(io_bpuTopDownInfo_rasMissBubble)
    );

    assign io_fromBpu_prediction_ready_o = io_fromBpu_prediction_ready;
    
    // Trace replay - simple sequential logic (Verilator compatible)
    reg [7:0] cycle_cnt;
    always @(posedge clk or posedge reset) begin
        if (reset) begin
            cycle_cnt <= 0;
            io_fromBpu_prediction_valid <= 0;
            io_fromBpu_meta_valid <= 0;
        end else begin
            cycle_cnt <= cycle_cnt + 1;
            // Key events from reference waveform
            case (cycle_cnt)
                8'd2: io_fromBpu_prediction_valid <= 1;  // was t=1132
                8'd3: io_fromBpu_meta_valid <= 1;        // was t=1136
                8'd7: io_fromBpu_meta_valid <= 0;        // was t=1158
            endcase
        end
    end
    
    // Tie off other inputs to sensible defaults
    initial begin
        io_fromBpu_prediction_bits_startPc_addr = 49'h1000000000000;
        io_fromBpu_prediction_bits_target_addr = 49'h1000000000000;
        io_fromBpu_prediction_bits_takenCfiOffset_valid = 0;
        io_fromBpu_prediction_bits_takenCfiOffset_bits = 0;
        io_fromBpu_prediction_bits_s3Override = 0;
    end
    
    // Finish after enough cycles
    always @(posedge clk) begin
        if (cycle_cnt == 8'd50) begin
            $finish;
        end
    end

endmodule