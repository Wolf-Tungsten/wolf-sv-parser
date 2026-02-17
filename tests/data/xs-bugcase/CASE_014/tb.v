`timescale 1ns/1ps

module xs_bugcase_tb (
    input  logic clk,
    input  logic rst_n,
    output logic io_toFtq_prediction_ready_o,
    output logic s1_fire_o,
    output logic abtb_io_stageCtrl_s0_fire_probe_o
);

    logic reset;
    assign reset = ~rst_n;

    logic [63:0] stim;
    wire [191:0] stim_wide = {3{stim}};
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            stim <= 64'h1;
        end else begin
            stim <= {stim[62:0], stim[63] ^ stim[62] ^ stim[60] ^ stim[59]};
        end
    end

    logic io_toFtq_prediction_ready;

    logic io_ctrl_ubtbEnable;
    logic io_ctrl_abtbEnable;
    logic io_ctrl_mbtbEnable;
    logic io_ctrl_tageEnable;
    logic io_ctrl_scEnable;
    logic io_ctrl_ittageEnable;
    logic [46:0] io_resetVector_addr;
    logic io_fromFtq_redirect_valid;
    logic [48:0] io_fromFtq_redirect_bits_cfiPc_addr;
    logic [48:0] io_fromFtq_redirect_bits_target_addr;
    logic io_fromFtq_redirect_bits_taken;
    logic [1:0] io_fromFtq_redirect_bits_attribute_branchType;
    logic [1:0] io_fromFtq_redirect_bits_attribute_rasAction;
    logic io_fromFtq_redirect_bits_meta_phr_phrPtr_flag;
    logic [9:0] io_fromFtq_redirect_bits_meta_phr_phrPtr_value;
    logic [12:0] io_fromFtq_redirect_bits_meta_phr_phrLowBits;
    logic [15:0] io_fromFtq_redirect_bits_meta_commonHRMeta_ghr;
    logic [7:0] io_fromFtq_redirect_bits_meta_commonHRMeta_bw;
    logic io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_0;
    logic io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_1;
    logic io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_2;
    logic io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_3;
    logic io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_4;
    logic io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_5;
    logic io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_6;
    logic io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_7;
    logic [1:0] io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_0_branchType;
    logic [1:0] io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_1_branchType;
    logic [1:0] io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_2_branchType;
    logic [1:0] io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_3_branchType;
    logic [1:0] io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_4_branchType;
    logic [1:0] io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_5_branchType;
    logic [1:0] io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_6_branchType;
    logic [1:0] io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_7_branchType;
    logic [4:0] io_fromFtq_redirect_bits_meta_commonHRMeta_position_0;
    logic [4:0] io_fromFtq_redirect_bits_meta_commonHRMeta_position_1;
    logic [4:0] io_fromFtq_redirect_bits_meta_commonHRMeta_position_2;
    logic [4:0] io_fromFtq_redirect_bits_meta_commonHRMeta_position_3;
    logic [4:0] io_fromFtq_redirect_bits_meta_commonHRMeta_position_4;
    logic [4:0] io_fromFtq_redirect_bits_meta_commonHRMeta_position_5;
    logic [4:0] io_fromFtq_redirect_bits_meta_commonHRMeta_position_6;
    logic [4:0] io_fromFtq_redirect_bits_meta_commonHRMeta_position_7;
    logic [3:0] io_fromFtq_redirect_bits_meta_ras_ssp;
    logic [2:0] io_fromFtq_redirect_bits_meta_ras_sctr;
    logic io_fromFtq_redirect_bits_meta_ras_tosw_flag;
    logic [4:0] io_fromFtq_redirect_bits_meta_ras_tosw_value;
    logic io_fromFtq_redirect_bits_meta_ras_tosr_flag;
    logic [4:0] io_fromFtq_redirect_bits_meta_ras_tosr_value;
    logic io_fromFtq_redirect_bits_meta_ras_nos_flag;
    logic [4:0] io_fromFtq_redirect_bits_meta_ras_nos_value;
    logic io_fromFtq_train_ready;
    logic io_fromFtq_train_valid;
    logic [48:0] io_fromFtq_train_bits_startPc_addr;
    logic io_fromFtq_train_bits_branches_0_valid;
    logic [48:0] io_fromFtq_train_bits_branches_0_bits_target_addr;
    logic io_fromFtq_train_bits_branches_0_bits_taken;
    logic [4:0] io_fromFtq_train_bits_branches_0_bits_cfiPosition;
    logic [1:0] io_fromFtq_train_bits_branches_0_bits_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_branches_0_bits_attribute_rasAction;
    logic io_fromFtq_train_bits_branches_0_bits_mispredict;
    logic io_fromFtq_train_bits_branches_1_valid;
    logic [48:0] io_fromFtq_train_bits_branches_1_bits_target_addr;
    logic io_fromFtq_train_bits_branches_1_bits_taken;
    logic [4:0] io_fromFtq_train_bits_branches_1_bits_cfiPosition;
    logic [1:0] io_fromFtq_train_bits_branches_1_bits_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_branches_1_bits_attribute_rasAction;
    logic io_fromFtq_train_bits_branches_1_bits_mispredict;
    logic io_fromFtq_train_bits_branches_2_valid;
    logic [48:0] io_fromFtq_train_bits_branches_2_bits_target_addr;
    logic io_fromFtq_train_bits_branches_2_bits_taken;
    logic [4:0] io_fromFtq_train_bits_branches_2_bits_cfiPosition;
    logic [1:0] io_fromFtq_train_bits_branches_2_bits_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_branches_2_bits_attribute_rasAction;
    logic io_fromFtq_train_bits_branches_2_bits_mispredict;
    logic io_fromFtq_train_bits_branches_3_valid;
    logic [48:0] io_fromFtq_train_bits_branches_3_bits_target_addr;
    logic io_fromFtq_train_bits_branches_3_bits_taken;
    logic [4:0] io_fromFtq_train_bits_branches_3_bits_cfiPosition;
    logic [1:0] io_fromFtq_train_bits_branches_3_bits_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_branches_3_bits_attribute_rasAction;
    logic io_fromFtq_train_bits_branches_3_bits_mispredict;
    logic io_fromFtq_train_bits_branches_4_valid;
    logic [48:0] io_fromFtq_train_bits_branches_4_bits_target_addr;
    logic io_fromFtq_train_bits_branches_4_bits_taken;
    logic [4:0] io_fromFtq_train_bits_branches_4_bits_cfiPosition;
    logic [1:0] io_fromFtq_train_bits_branches_4_bits_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_branches_4_bits_attribute_rasAction;
    logic io_fromFtq_train_bits_branches_4_bits_mispredict;
    logic io_fromFtq_train_bits_branches_5_valid;
    logic [48:0] io_fromFtq_train_bits_branches_5_bits_target_addr;
    logic io_fromFtq_train_bits_branches_5_bits_taken;
    logic [4:0] io_fromFtq_train_bits_branches_5_bits_cfiPosition;
    logic [1:0] io_fromFtq_train_bits_branches_5_bits_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_branches_5_bits_attribute_rasAction;
    logic io_fromFtq_train_bits_branches_5_bits_mispredict;
    logic io_fromFtq_train_bits_branches_6_valid;
    logic [48:0] io_fromFtq_train_bits_branches_6_bits_target_addr;
    logic io_fromFtq_train_bits_branches_6_bits_taken;
    logic [4:0] io_fromFtq_train_bits_branches_6_bits_cfiPosition;
    logic [1:0] io_fromFtq_train_bits_branches_6_bits_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_branches_6_bits_attribute_rasAction;
    logic io_fromFtq_train_bits_branches_6_bits_mispredict;
    logic io_fromFtq_train_bits_branches_7_valid;
    logic [48:0] io_fromFtq_train_bits_branches_7_bits_target_addr;
    logic io_fromFtq_train_bits_branches_7_bits_taken;
    logic [4:0] io_fromFtq_train_bits_branches_7_bits_cfiPosition;
    logic [1:0] io_fromFtq_train_bits_branches_7_bits_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_branches_7_bits_attribute_rasAction;
    logic io_fromFtq_train_bits_branches_7_bits_mispredict;
    logic io_fromFtq_train_bits_meta_mbtb_entries_0_0_rawHit;
    logic [4:0] io_fromFtq_train_bits_meta_mbtb_entries_0_0_position;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_rasAction;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_0_counter_value;
    logic io_fromFtq_train_bits_meta_mbtb_entries_0_1_rawHit;
    logic [4:0] io_fromFtq_train_bits_meta_mbtb_entries_0_1_position;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_rasAction;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_1_counter_value;
    logic io_fromFtq_train_bits_meta_mbtb_entries_0_2_rawHit;
    logic [4:0] io_fromFtq_train_bits_meta_mbtb_entries_0_2_position;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_rasAction;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_2_counter_value;
    logic io_fromFtq_train_bits_meta_mbtb_entries_0_3_rawHit;
    logic [4:0] io_fromFtq_train_bits_meta_mbtb_entries_0_3_position;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_rasAction;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_0_3_counter_value;
    logic io_fromFtq_train_bits_meta_mbtb_entries_1_0_rawHit;
    logic [4:0] io_fromFtq_train_bits_meta_mbtb_entries_1_0_position;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_rasAction;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_0_counter_value;
    logic io_fromFtq_train_bits_meta_mbtb_entries_1_1_rawHit;
    logic [4:0] io_fromFtq_train_bits_meta_mbtb_entries_1_1_position;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_rasAction;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_1_counter_value;
    logic io_fromFtq_train_bits_meta_mbtb_entries_1_2_rawHit;
    logic [4:0] io_fromFtq_train_bits_meta_mbtb_entries_1_2_position;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_rasAction;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_2_counter_value;
    logic io_fromFtq_train_bits_meta_mbtb_entries_1_3_rawHit;
    logic [4:0] io_fromFtq_train_bits_meta_mbtb_entries_1_3_position;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_branchType;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_rasAction;
    logic [1:0] io_fromFtq_train_bits_meta_mbtb_entries_1_3_counter_value;
    logic io_fromFtq_train_bits_meta_tage_entries_0_useProvider;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_0_providerTableIdx;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_0_providerWayIdx;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_0_providerTakenCtr_value;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_0_providerUsefulCtr_value;
    logic io_fromFtq_train_bits_meta_tage_entries_0_altOrBasePred;
    logic io_fromFtq_train_bits_meta_tage_entries_1_useProvider;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_1_providerTableIdx;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_1_providerWayIdx;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_1_providerTakenCtr_value;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_1_providerUsefulCtr_value;
    logic io_fromFtq_train_bits_meta_tage_entries_1_altOrBasePred;
    logic io_fromFtq_train_bits_meta_tage_entries_2_useProvider;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_2_providerTableIdx;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_2_providerWayIdx;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_2_providerTakenCtr_value;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_2_providerUsefulCtr_value;
    logic io_fromFtq_train_bits_meta_tage_entries_2_altOrBasePred;
    logic io_fromFtq_train_bits_meta_tage_entries_3_useProvider;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_3_providerTableIdx;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_3_providerWayIdx;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_3_providerTakenCtr_value;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_3_providerUsefulCtr_value;
    logic io_fromFtq_train_bits_meta_tage_entries_3_altOrBasePred;
    logic io_fromFtq_train_bits_meta_tage_entries_4_useProvider;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_4_providerTableIdx;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_4_providerWayIdx;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_4_providerTakenCtr_value;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_4_providerUsefulCtr_value;
    logic io_fromFtq_train_bits_meta_tage_entries_4_altOrBasePred;
    logic io_fromFtq_train_bits_meta_tage_entries_5_useProvider;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_5_providerTableIdx;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_5_providerWayIdx;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_5_providerTakenCtr_value;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_5_providerUsefulCtr_value;
    logic io_fromFtq_train_bits_meta_tage_entries_5_altOrBasePred;
    logic io_fromFtq_train_bits_meta_tage_entries_6_useProvider;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_6_providerTableIdx;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_6_providerWayIdx;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_6_providerTakenCtr_value;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_6_providerUsefulCtr_value;
    logic io_fromFtq_train_bits_meta_tage_entries_6_altOrBasePred;
    logic io_fromFtq_train_bits_meta_tage_entries_7_useProvider;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_7_providerTableIdx;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_7_providerWayIdx;
    logic [2:0] io_fromFtq_train_bits_meta_tage_entries_7_providerTakenCtr_value;
    logic [1:0] io_fromFtq_train_bits_meta_tage_entries_7_providerUsefulCtr_value;
    logic io_fromFtq_train_bits_meta_tage_entries_7_altOrBasePred;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_0_0;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_0_1;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_0_2;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_0_3;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_0_4;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_0_5;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_0_6;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_0_7;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_1_0;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_1_1;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_1_2;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_1_3;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_1_4;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_1_5;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_1_6;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scPathResp_1_7;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_0;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_1;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_2;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_3;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_4;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_5;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_6;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_7;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_8;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_9;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_10;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_11;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_12;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_13;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_14;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_15;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_16;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_17;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_18;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_19;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_20;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_21;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_22;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_23;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_24;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_25;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_26;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_27;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_28;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_29;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_30;
    logic [5:0] io_fromFtq_train_bits_meta_sc_scBiasResp_31;
    logic [1:0] io_fromFtq_train_bits_meta_sc_scBiasLowerBits_0;
    logic [1:0] io_fromFtq_train_bits_meta_sc_scBiasLowerBits_1;
    logic [1:0] io_fromFtq_train_bits_meta_sc_scBiasLowerBits_2;
    logic [1:0] io_fromFtq_train_bits_meta_sc_scBiasLowerBits_3;
    logic [1:0] io_fromFtq_train_bits_meta_sc_scBiasLowerBits_4;
    logic [1:0] io_fromFtq_train_bits_meta_sc_scBiasLowerBits_5;
    logic [1:0] io_fromFtq_train_bits_meta_sc_scBiasLowerBits_6;
    logic [1:0] io_fromFtq_train_bits_meta_sc_scBiasLowerBits_7;
    logic io_fromFtq_train_bits_meta_sc_scCommonHR_valid;
    logic [15:0] io_fromFtq_train_bits_meta_sc_scCommonHR_ghr;
    logic [7:0] io_fromFtq_train_bits_meta_sc_scCommonHR_bw;
    logic io_fromFtq_train_bits_meta_sc_scPred_0;
    logic io_fromFtq_train_bits_meta_sc_scPred_1;
    logic io_fromFtq_train_bits_meta_sc_scPred_2;
    logic io_fromFtq_train_bits_meta_sc_scPred_3;
    logic io_fromFtq_train_bits_meta_sc_scPred_4;
    logic io_fromFtq_train_bits_meta_sc_scPred_5;
    logic io_fromFtq_train_bits_meta_sc_scPred_6;
    logic io_fromFtq_train_bits_meta_sc_scPred_7;
    logic io_fromFtq_train_bits_meta_sc_tagePred_0;
    logic io_fromFtq_train_bits_meta_sc_tagePred_1;
    logic io_fromFtq_train_bits_meta_sc_tagePred_2;
    logic io_fromFtq_train_bits_meta_sc_tagePred_3;
    logic io_fromFtq_train_bits_meta_sc_tagePred_4;
    logic io_fromFtq_train_bits_meta_sc_tagePred_5;
    logic io_fromFtq_train_bits_meta_sc_tagePred_6;
    logic io_fromFtq_train_bits_meta_sc_tagePred_7;
    logic io_fromFtq_train_bits_meta_sc_tagePredValid_0;
    logic io_fromFtq_train_bits_meta_sc_tagePredValid_1;
    logic io_fromFtq_train_bits_meta_sc_tagePredValid_2;
    logic io_fromFtq_train_bits_meta_sc_tagePredValid_3;
    logic io_fromFtq_train_bits_meta_sc_tagePredValid_4;
    logic io_fromFtq_train_bits_meta_sc_tagePredValid_5;
    logic io_fromFtq_train_bits_meta_sc_tagePredValid_6;
    logic io_fromFtq_train_bits_meta_sc_tagePredValid_7;
    logic io_fromFtq_train_bits_meta_sc_useScPred_0;
    logic io_fromFtq_train_bits_meta_sc_useScPred_1;
    logic io_fromFtq_train_bits_meta_sc_useScPred_2;
    logic io_fromFtq_train_bits_meta_sc_useScPred_3;
    logic io_fromFtq_train_bits_meta_sc_useScPred_4;
    logic io_fromFtq_train_bits_meta_sc_useScPred_5;
    logic io_fromFtq_train_bits_meta_sc_useScPred_6;
    logic io_fromFtq_train_bits_meta_sc_useScPred_7;
    logic io_fromFtq_train_bits_meta_sc_sumAboveThres_0;
    logic io_fromFtq_train_bits_meta_sc_sumAboveThres_1;
    logic io_fromFtq_train_bits_meta_sc_sumAboveThres_2;
    logic io_fromFtq_train_bits_meta_sc_sumAboveThres_3;
    logic io_fromFtq_train_bits_meta_sc_sumAboveThres_4;
    logic io_fromFtq_train_bits_meta_sc_sumAboveThres_5;
    logic io_fromFtq_train_bits_meta_sc_sumAboveThres_6;
    logic io_fromFtq_train_bits_meta_sc_sumAboveThres_7;
    logic io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_0;
    logic io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_1;
    logic io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_2;
    logic io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_3;
    logic io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_4;
    logic io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_5;
    logic io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_6;
    logic io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_7;
    logic io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_0;
    logic io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_1;
    logic io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_2;
    logic io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_3;
    logic io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_4;
    logic io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_5;
    logic io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_6;
    logic io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_7;
    logic [6:0] io_fromFtq_train_bits_meta_sc_debug_predPathIdx_0;
    logic [6:0] io_fromFtq_train_bits_meta_sc_debug_predPathIdx_1;
    logic [6:0] io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_0;
    logic [6:0] io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_1;
    logic [6:0] io_fromFtq_train_bits_meta_sc_debug_predBWIdx_0;
    logic [6:0] io_fromFtq_train_bits_meta_sc_debug_predBWIdx_1;
    logic [6:0] io_fromFtq_train_bits_meta_sc_debug_predBiasIdx;
    logic io_fromFtq_train_bits_meta_ittage_provider_valid;
    logic [2:0] io_fromFtq_train_bits_meta_ittage_provider_bits;
    logic io_fromFtq_train_bits_meta_ittage_altProvider_valid;
    logic [2:0] io_fromFtq_train_bits_meta_ittage_altProvider_bits;
    logic io_fromFtq_train_bits_meta_ittage_altDiffers;
    logic io_fromFtq_train_bits_meta_ittage_providerUsefulCnt_value;
    logic [1:0] io_fromFtq_train_bits_meta_ittage_providerCnt_value;
    logic [1:0] io_fromFtq_train_bits_meta_ittage_altProviderCnt_value;
    logic io_fromFtq_train_bits_meta_ittage_allocate_valid;
    logic [2:0] io_fromFtq_train_bits_meta_ittage_allocate_bits;
    logic [48:0] io_fromFtq_train_bits_meta_ittage_providerTarget_addr;
    logic [48:0] io_fromFtq_train_bits_meta_ittage_altProviderTarget_addr;
    logic [9:0] io_fromFtq_train_bits_meta_phr_phrPtr_value;
    logic [12:0] io_fromFtq_train_bits_meta_phr_phrLowBits;
    logic [12:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist;
    logic [11:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist;
    logic [12:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist;
    logic [11:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist;
    logic [12:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist;
    logic [11:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist;
    logic [12:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist;
    logic [11:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist;
    logic [7:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist;
    logic [12:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist;
    logic [11:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist;
    logic [12:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist;
    logic [11:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist;
    logic [11:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist;
    logic [10:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist;
    logic [7:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist;
    logic [6:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist;
    logic [7:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist;
    logic [8:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist;
    logic [7:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist;
    logic [7:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist;
    logic [6:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist;
    logic [3:0] io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist;
    logic io_fromFtq_commit_valid;
    logic [3:0] io_fromFtq_commit_bits_meta_ras_ssp;
    logic io_fromFtq_commit_bits_meta_ras_tosw_flag;
    logic [4:0] io_fromFtq_commit_bits_meta_ras_tosw_value;
    logic [1:0] io_fromFtq_commit_bits_attribute_rasAction;
    logic io_fromFtq_bpuPtr_flag;
    logic [5:0] io_fromFtq_bpuPtr_value;
    logic io_toFtq_prediction_valid;
    logic [48:0] io_toFtq_prediction_bits_startPc_addr;
    logic [48:0] io_toFtq_prediction_bits_target_addr;
    logic io_toFtq_prediction_bits_takenCfiOffset_valid;
    logic [4:0] io_toFtq_prediction_bits_takenCfiOffset_bits;
    logic io_toFtq_prediction_bits_s3Override;
    logic io_toFtq_meta_valid;
    logic io_toFtq_meta_bits_redirectMeta_phr_phrPtr_flag;
    logic [9:0] io_toFtq_meta_bits_redirectMeta_phr_phrPtr_value;
    logic [12:0] io_toFtq_meta_bits_redirectMeta_phr_phrLowBits;
    logic [15:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_ghr;
    logic [7:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_bw;
    logic io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_0;
    logic io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_1;
    logic io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_2;
    logic io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_3;
    logic io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_4;
    logic io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_5;
    logic io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_6;
    logic io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_7;
    logic [1:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_0_branchType;
    logic [1:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_1_branchType;
    logic [1:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_2_branchType;
    logic [1:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_3_branchType;
    logic [1:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_4_branchType;
    logic [1:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_5_branchType;
    logic [1:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_6_branchType;
    logic [1:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_7_branchType;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_0;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_1;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_2;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_3;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_4;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_5;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_6;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_7;
    logic [3:0] io_toFtq_meta_bits_redirectMeta_ras_ssp;
    logic [2:0] io_toFtq_meta_bits_redirectMeta_ras_sctr;
    logic io_toFtq_meta_bits_redirectMeta_ras_tosw_flag;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_ras_tosw_value;
    logic io_toFtq_meta_bits_redirectMeta_ras_tosr_flag;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_ras_tosr_value;
    logic io_toFtq_meta_bits_redirectMeta_ras_nos_flag;
    logic [4:0] io_toFtq_meta_bits_redirectMeta_ras_nos_value;
    logic [48:0] io_toFtq_meta_bits_redirectMeta_ras_topRetAddr_addr;
    logic io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_rawHit;
    logic [4:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_position;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_branchType;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_rasAction;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_counter_value;
    logic io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_rawHit;
    logic [4:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_position;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_branchType;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_rasAction;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_counter_value;
    logic io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_rawHit;
    logic [4:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_position;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_branchType;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_rasAction;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_counter_value;
    logic io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_rawHit;
    logic [4:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_position;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_branchType;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_rasAction;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_counter_value;
    logic io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_rawHit;
    logic [4:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_position;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_branchType;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_rasAction;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_counter_value;
    logic io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_rawHit;
    logic [4:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_position;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_branchType;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_rasAction;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_counter_value;
    logic io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_rawHit;
    logic [4:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_position;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_branchType;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_rasAction;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_counter_value;
    logic io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_rawHit;
    logic [4:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_position;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_branchType;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_rasAction;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_counter_value;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_0_useProvider;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerTableIdx;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerWayIdx;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerTakenCtr_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerUsefulCtr_value;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_0_altOrBasePred;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_1_useProvider;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerTableIdx;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerWayIdx;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerTakenCtr_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerUsefulCtr_value;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_1_altOrBasePred;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_2_useProvider;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerTableIdx;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerWayIdx;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerTakenCtr_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerUsefulCtr_value;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_2_altOrBasePred;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_3_useProvider;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerTableIdx;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerWayIdx;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerTakenCtr_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerUsefulCtr_value;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_3_altOrBasePred;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_4_useProvider;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerTableIdx;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerWayIdx;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerTakenCtr_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerUsefulCtr_value;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_4_altOrBasePred;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_5_useProvider;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerTableIdx;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerWayIdx;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerTakenCtr_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerUsefulCtr_value;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_5_altOrBasePred;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_6_useProvider;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerTableIdx;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerWayIdx;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerTakenCtr_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerUsefulCtr_value;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_6_altOrBasePred;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_7_useProvider;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerTableIdx;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerWayIdx;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerTakenCtr_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerUsefulCtr_value;
    logic io_toFtq_meta_bits_resolveMeta_tage_entries_7_altOrBasePred;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_0;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_1;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_2;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_3;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_4;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_5;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_6;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_7;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_0;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_1;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_2;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_3;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_4;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_5;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_6;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_7;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_0;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_1;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_2;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_3;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_4;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_5;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_6;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_7;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_8;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_9;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_10;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_11;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_12;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_13;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_14;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_15;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_16;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_17;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_18;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_19;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_20;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_21;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_22;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_23;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_24;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_25;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_26;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_27;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_28;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_29;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_30;
    logic [5:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_31;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_0;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_1;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_2;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_3;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_4;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_5;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_6;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_7;
    logic io_toFtq_meta_bits_resolveMeta_sc_scCommonHR_valid;
    logic [15:0] io_toFtq_meta_bits_resolveMeta_sc_scCommonHR_ghr;
    logic [7:0] io_toFtq_meta_bits_resolveMeta_sc_scCommonHR_bw;
    logic io_toFtq_meta_bits_resolveMeta_sc_scPred_0;
    logic io_toFtq_meta_bits_resolveMeta_sc_scPred_1;
    logic io_toFtq_meta_bits_resolveMeta_sc_scPred_2;
    logic io_toFtq_meta_bits_resolveMeta_sc_scPred_3;
    logic io_toFtq_meta_bits_resolveMeta_sc_scPred_4;
    logic io_toFtq_meta_bits_resolveMeta_sc_scPred_5;
    logic io_toFtq_meta_bits_resolveMeta_sc_scPred_6;
    logic io_toFtq_meta_bits_resolveMeta_sc_scPred_7;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePred_0;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePred_1;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePred_2;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePred_3;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePred_4;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePred_5;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePred_6;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePred_7;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_0;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_1;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_2;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_3;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_4;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_5;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_6;
    logic io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_7;
    logic io_toFtq_meta_bits_resolveMeta_sc_useScPred_0;
    logic io_toFtq_meta_bits_resolveMeta_sc_useScPred_1;
    logic io_toFtq_meta_bits_resolveMeta_sc_useScPred_2;
    logic io_toFtq_meta_bits_resolveMeta_sc_useScPred_3;
    logic io_toFtq_meta_bits_resolveMeta_sc_useScPred_4;
    logic io_toFtq_meta_bits_resolveMeta_sc_useScPred_5;
    logic io_toFtq_meta_bits_resolveMeta_sc_useScPred_6;
    logic io_toFtq_meta_bits_resolveMeta_sc_useScPred_7;
    logic io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_0;
    logic io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_1;
    logic io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_2;
    logic io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_3;
    logic io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_4;
    logic io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_5;
    logic io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_6;
    logic io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_7;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_0;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_1;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_2;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_3;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_4;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_5;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_6;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_7;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_0;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_1;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_2;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_3;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_4;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_5;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_6;
    logic io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_7;
    logic [6:0] io_toFtq_meta_bits_resolveMeta_sc_debug_predPathIdx_0;
    logic [6:0] io_toFtq_meta_bits_resolveMeta_sc_debug_predPathIdx_1;
    logic [6:0] io_toFtq_meta_bits_resolveMeta_sc_debug_predGlobalIdx_0;
    logic [6:0] io_toFtq_meta_bits_resolveMeta_sc_debug_predGlobalIdx_1;
    logic [6:0] io_toFtq_meta_bits_resolveMeta_sc_debug_predBWIdx_0;
    logic [6:0] io_toFtq_meta_bits_resolveMeta_sc_debug_predBWIdx_1;
    logic [6:0] io_toFtq_meta_bits_resolveMeta_sc_debug_predBiasIdx;
    logic io_toFtq_meta_bits_resolveMeta_ittage_provider_valid;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_ittage_provider_bits;
    logic io_toFtq_meta_bits_resolveMeta_ittage_altProvider_valid;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_ittage_altProvider_bits;
    logic io_toFtq_meta_bits_resolveMeta_ittage_altDiffers;
    logic io_toFtq_meta_bits_resolveMeta_ittage_providerUsefulCnt_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_ittage_providerCnt_value;
    logic [1:0] io_toFtq_meta_bits_resolveMeta_ittage_altProviderCnt_value;
    logic io_toFtq_meta_bits_resolveMeta_ittage_allocate_valid;
    logic [2:0] io_toFtq_meta_bits_resolveMeta_ittage_allocate_bits;
    logic [48:0] io_toFtq_meta_bits_resolveMeta_ittage_providerTarget_addr;
    logic [48:0] io_toFtq_meta_bits_resolveMeta_ittage_altProviderTarget_addr;
    logic [9:0] io_toFtq_meta_bits_resolveMeta_phr_phrPtr_value;
    logic [12:0] io_toFtq_meta_bits_resolveMeta_phr_phrLowBits;
    logic [12:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_31_foldedHist;
    logic [11:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_30_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_29_foldedHist;
    logic [12:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_28_foldedHist;
    logic [11:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_27_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_26_foldedHist;
    logic [12:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_25_foldedHist;
    logic [11:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_24_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_23_foldedHist;
    logic [12:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_22_foldedHist;
    logic [11:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_21_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_20_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_19_foldedHist;
    logic [7:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_18_foldedHist;
    logic [12:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_17_foldedHist;
    logic [11:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_16_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_15_foldedHist;
    logic [12:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_14_foldedHist;
    logic [11:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_13_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_12_foldedHist;
    logic [11:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_11_foldedHist;
    logic [10:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_10_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_9_foldedHist;
    logic [7:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_8_foldedHist;
    logic [6:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_7_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_6_foldedHist;
    logic [7:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_5_foldedHist;
    logic [8:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_4_foldedHist;
    logic [7:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_3_foldedHist;
    logic [7:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_2_foldedHist;
    logic [6:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_1_foldedHist;
    logic [3:0] io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_0_foldedHist;
    logic [3:0] io_toFtq_meta_bits_commitMeta_ras_ssp;
    logic io_toFtq_meta_bits_commitMeta_ras_tosw_flag;
    logic [4:0] io_toFtq_meta_bits_commitMeta_ras_tosw_value;
    logic io_toFtq_s3FtqPtr_flag;
    logic [5:0] io_toFtq_s3FtqPtr_value;
    logic [4:0] io_toFtq_perfMeta_s1Prediction_cfiPosition;
    logic [48:0] io_toFtq_perfMeta_s1Prediction_target_addr;
    logic [1:0] io_toFtq_perfMeta_s1Prediction_attribute_branchType;
    logic [1:0] io_toFtq_perfMeta_s1Prediction_attribute_rasAction;
    logic io_toFtq_perfMeta_s1Prediction_taken;
    logic [4:0] io_toFtq_perfMeta_s3Prediction_cfiPosition;
    logic [48:0] io_toFtq_perfMeta_s3Prediction_target_addr;
    logic [1:0] io_toFtq_perfMeta_s3Prediction_attribute_branchType;
    logic [1:0] io_toFtq_perfMeta_s3Prediction_attribute_rasAction;
    logic io_toFtq_perfMeta_s3Prediction_taken;
    logic io_toFtq_perfMeta_mbtbMeta_entries_0_0_rawHit;
    logic [4:0] io_toFtq_perfMeta_mbtbMeta_entries_0_0_position;
    logic io_toFtq_perfMeta_mbtbMeta_entries_0_1_rawHit;
    logic [4:0] io_toFtq_perfMeta_mbtbMeta_entries_0_1_position;
    logic io_toFtq_perfMeta_mbtbMeta_entries_0_2_rawHit;
    logic [4:0] io_toFtq_perfMeta_mbtbMeta_entries_0_2_position;
    logic io_toFtq_perfMeta_mbtbMeta_entries_0_3_rawHit;
    logic [4:0] io_toFtq_perfMeta_mbtbMeta_entries_0_3_position;
    logic io_toFtq_perfMeta_mbtbMeta_entries_1_0_rawHit;
    logic [4:0] io_toFtq_perfMeta_mbtbMeta_entries_1_0_position;
    logic io_toFtq_perfMeta_mbtbMeta_entries_1_1_rawHit;
    logic [4:0] io_toFtq_perfMeta_mbtbMeta_entries_1_1_position;
    logic io_toFtq_perfMeta_mbtbMeta_entries_1_2_rawHit;
    logic [4:0] io_toFtq_perfMeta_mbtbMeta_entries_1_2_position;
    logic io_toFtq_perfMeta_mbtbMeta_entries_1_3_rawHit;
    logic [4:0] io_toFtq_perfMeta_mbtbMeta_entries_1_3_position;
    logic [2:0] io_toFtq_perfMeta_bpSource_s1Source;
    logic [2:0] io_toFtq_perfMeta_bpSource_s3Source;
    logic io_toFtq_perfMeta_bpSource_s3Override;
    logic [7:0] boreChildrenBd_bore_array;
    logic boreChildrenBd_bore_all;
    logic boreChildrenBd_bore_req;
    logic boreChildrenBd_bore_ack;
    logic boreChildrenBd_bore_writeen;
    logic [37:0] boreChildrenBd_bore_be;
    logic [9:0] boreChildrenBd_bore_addr;
    logic [111:0] boreChildrenBd_bore_indata;
    logic boreChildrenBd_bore_readen;
    logic [9:0] boreChildrenBd_bore_addr_rd;
    logic [111:0] boreChildrenBd_bore_outdata;
    logic [7:0] boreChildrenBd_bore_1_array;
    logic boreChildrenBd_bore_1_all;
    logic boreChildrenBd_bore_1_req;
    logic boreChildrenBd_bore_1_ack;
    logic boreChildrenBd_bore_1_writeen;
    logic [37:0] boreChildrenBd_bore_1_be;
    logic [7:0] boreChildrenBd_bore_1_addr;
    logic [37:0] boreChildrenBd_bore_1_indata;
    logic boreChildrenBd_bore_1_readen;
    logic [7:0] boreChildrenBd_bore_1_addr_rd;
    logic [37:0] boreChildrenBd_bore_1_outdata;
    logic [7:0] boreChildrenBd_bore_2_array;
    logic boreChildrenBd_bore_2_all;
    logic boreChildrenBd_bore_2_req;
    logic boreChildrenBd_bore_2_ack;
    logic boreChildrenBd_bore_2_writeen;
    logic [75:0] boreChildrenBd_bore_2_be;
    logic [7:0] boreChildrenBd_bore_2_addr;
    logic [75:0] boreChildrenBd_bore_2_indata;
    logic boreChildrenBd_bore_2_readen;
    logic [7:0] boreChildrenBd_bore_2_addr_rd;
    logic [75:0] boreChildrenBd_bore_2_outdata;
    logic [7:0] boreChildrenBd_bore_3_array;
    logic boreChildrenBd_bore_3_all;
    logic boreChildrenBd_bore_3_req;
    logic boreChildrenBd_bore_3_ack;
    logic boreChildrenBd_bore_3_writeen;
    logic [75:0] boreChildrenBd_bore_3_be;
    logic [7:0] boreChildrenBd_bore_3_addr;
    logic [75:0] boreChildrenBd_bore_3_indata;
    logic boreChildrenBd_bore_3_readen;
    logic [7:0] boreChildrenBd_bore_3_addr_rd;
    logic [75:0] boreChildrenBd_bore_3_outdata;
    logic [7:0] boreChildrenBd_bore_4_array;
    logic boreChildrenBd_bore_4_all;
    logic boreChildrenBd_bore_4_req;
    logic boreChildrenBd_bore_4_ack;
    logic boreChildrenBd_bore_4_writeen;
    logic [75:0] boreChildrenBd_bore_4_be;
    logic [7:0] boreChildrenBd_bore_4_addr;
    logic [75:0] boreChildrenBd_bore_4_indata;
    logic boreChildrenBd_bore_4_readen;
    logic [7:0] boreChildrenBd_bore_4_addr_rd;
    logic [75:0] boreChildrenBd_bore_4_outdata;
    logic [7:0] boreChildrenBd_bore_5_addr;
    logic [7:0] boreChildrenBd_bore_5_addr_rd;
    logic [47:0] boreChildrenBd_bore_5_wdata;
    logic [7:0] boreChildrenBd_bore_5_wmask;
    logic boreChildrenBd_bore_5_re;
    logic boreChildrenBd_bore_5_we;
    logic [47:0] boreChildrenBd_bore_5_rdata;
    logic boreChildrenBd_bore_5_ack;
    logic boreChildrenBd_bore_5_selectedOH;
    logic [7:0] boreChildrenBd_bore_5_array;
    logic [7:0] boreChildrenBd_bore_6_addr;
    logic [7:0] boreChildrenBd_bore_6_addr_rd;
    logic [47:0] boreChildrenBd_bore_6_wdata;
    logic [7:0] boreChildrenBd_bore_6_wmask;
    logic boreChildrenBd_bore_6_re;
    logic boreChildrenBd_bore_6_we;
    logic [47:0] boreChildrenBd_bore_6_rdata;
    logic boreChildrenBd_bore_6_ack;
    logic boreChildrenBd_bore_6_selectedOH;
    logic [7:0] boreChildrenBd_bore_6_array;
    logic [7:0] boreChildrenBd_bore_7_addr;
    logic [7:0] boreChildrenBd_bore_7_addr_rd;
    logic [47:0] boreChildrenBd_bore_7_wdata;
    logic [7:0] boreChildrenBd_bore_7_wmask;
    logic boreChildrenBd_bore_7_re;
    logic boreChildrenBd_bore_7_we;
    logic [47:0] boreChildrenBd_bore_7_rdata;
    logic boreChildrenBd_bore_7_ack;
    logic boreChildrenBd_bore_7_selectedOH;
    logic [7:0] boreChildrenBd_bore_7_array;
    logic [7:0] boreChildrenBd_bore_8_addr;
    logic [7:0] boreChildrenBd_bore_8_addr_rd;
    logic [47:0] boreChildrenBd_bore_8_wdata;
    logic [7:0] boreChildrenBd_bore_8_wmask;
    logic boreChildrenBd_bore_8_re;
    logic boreChildrenBd_bore_8_we;
    logic [47:0] boreChildrenBd_bore_8_rdata;
    logic boreChildrenBd_bore_8_ack;
    logic boreChildrenBd_bore_8_selectedOH;
    logic [7:0] boreChildrenBd_bore_8_array;
    logic [7:0] boreChildrenBd_bore_9_addr;
    logic [7:0] boreChildrenBd_bore_9_addr_rd;
    logic [47:0] boreChildrenBd_bore_9_wdata;
    logic [7:0] boreChildrenBd_bore_9_wmask;
    logic boreChildrenBd_bore_9_re;
    logic boreChildrenBd_bore_9_we;
    logic [47:0] boreChildrenBd_bore_9_rdata;
    logic boreChildrenBd_bore_9_ack;
    logic boreChildrenBd_bore_9_selectedOH;
    logic [7:0] boreChildrenBd_bore_9_array;
    logic [7:0] boreChildrenBd_bore_10_addr;
    logic [7:0] boreChildrenBd_bore_10_addr_rd;
    logic [47:0] boreChildrenBd_bore_10_wdata;
    logic [7:0] boreChildrenBd_bore_10_wmask;
    logic boreChildrenBd_bore_10_re;
    logic boreChildrenBd_bore_10_we;
    logic [47:0] boreChildrenBd_bore_10_rdata;
    logic boreChildrenBd_bore_10_ack;
    logic boreChildrenBd_bore_10_selectedOH;
    logic [7:0] boreChildrenBd_bore_10_array;
    logic [7:0] boreChildrenBd_bore_11_addr;
    logic [7:0] boreChildrenBd_bore_11_addr_rd;
    logic [47:0] boreChildrenBd_bore_11_wdata;
    logic [7:0] boreChildrenBd_bore_11_wmask;
    logic boreChildrenBd_bore_11_re;
    logic boreChildrenBd_bore_11_we;
    logic [47:0] boreChildrenBd_bore_11_rdata;
    logic boreChildrenBd_bore_11_ack;
    logic boreChildrenBd_bore_11_selectedOH;
    logic [7:0] boreChildrenBd_bore_11_array;
    logic [7:0] boreChildrenBd_bore_12_addr;
    logic [7:0] boreChildrenBd_bore_12_addr_rd;
    logic [47:0] boreChildrenBd_bore_12_wdata;
    logic [7:0] boreChildrenBd_bore_12_wmask;
    logic boreChildrenBd_bore_12_re;
    logic boreChildrenBd_bore_12_we;
    logic [47:0] boreChildrenBd_bore_12_rdata;
    logic boreChildrenBd_bore_12_ack;
    logic boreChildrenBd_bore_12_selectedOH;
    logic [7:0] boreChildrenBd_bore_12_array;
    logic [7:0] boreChildrenBd_bore_13_addr;
    logic [7:0] boreChildrenBd_bore_13_addr_rd;
    logic [47:0] boreChildrenBd_bore_13_wdata;
    logic [7:0] boreChildrenBd_bore_13_wmask;
    logic boreChildrenBd_bore_13_re;
    logic boreChildrenBd_bore_13_we;
    logic [47:0] boreChildrenBd_bore_13_rdata;
    logic boreChildrenBd_bore_13_ack;
    logic boreChildrenBd_bore_13_selectedOH;
    logic [7:0] boreChildrenBd_bore_13_array;
    logic [7:0] boreChildrenBd_bore_14_addr;
    logic [7:0] boreChildrenBd_bore_14_addr_rd;
    logic [47:0] boreChildrenBd_bore_14_wdata;
    logic [7:0] boreChildrenBd_bore_14_wmask;
    logic boreChildrenBd_bore_14_re;
    logic boreChildrenBd_bore_14_we;
    logic [47:0] boreChildrenBd_bore_14_rdata;
    logic boreChildrenBd_bore_14_ack;
    logic boreChildrenBd_bore_14_selectedOH;
    logic [7:0] boreChildrenBd_bore_14_array;
    logic [7:0] boreChildrenBd_bore_15_addr;
    logic [7:0] boreChildrenBd_bore_15_addr_rd;
    logic [47:0] boreChildrenBd_bore_15_wdata;
    logic [7:0] boreChildrenBd_bore_15_wmask;
    logic boreChildrenBd_bore_15_re;
    logic boreChildrenBd_bore_15_we;
    logic [47:0] boreChildrenBd_bore_15_rdata;
    logic boreChildrenBd_bore_15_ack;
    logic boreChildrenBd_bore_15_selectedOH;
    logic [7:0] boreChildrenBd_bore_15_array;
    logic [7:0] boreChildrenBd_bore_16_addr;
    logic [7:0] boreChildrenBd_bore_16_addr_rd;
    logic [47:0] boreChildrenBd_bore_16_wdata;
    logic [7:0] boreChildrenBd_bore_16_wmask;
    logic boreChildrenBd_bore_16_re;
    logic boreChildrenBd_bore_16_we;
    logic [47:0] boreChildrenBd_bore_16_rdata;
    logic boreChildrenBd_bore_16_ack;
    logic boreChildrenBd_bore_16_selectedOH;
    logic [7:0] boreChildrenBd_bore_16_array;
    logic [7:0] boreChildrenBd_bore_17_addr;
    logic [7:0] boreChildrenBd_bore_17_addr_rd;
    logic [191:0] boreChildrenBd_bore_17_wdata;
    logic [31:0] boreChildrenBd_bore_17_wmask;
    logic boreChildrenBd_bore_17_re;
    logic boreChildrenBd_bore_17_we;
    logic [191:0] boreChildrenBd_bore_17_rdata;
    logic boreChildrenBd_bore_17_ack;
    logic boreChildrenBd_bore_17_selectedOH;
    logic [7:0] boreChildrenBd_bore_17_array;
    logic [7:0] boreChildrenBd_bore_18_addr;
    logic [7:0] boreChildrenBd_bore_18_addr_rd;
    logic [191:0] boreChildrenBd_bore_18_wdata;
    logic [31:0] boreChildrenBd_bore_18_wmask;
    logic boreChildrenBd_bore_18_re;
    logic boreChildrenBd_bore_18_we;
    logic [191:0] boreChildrenBd_bore_18_rdata;
    logic boreChildrenBd_bore_18_ack;
    logic boreChildrenBd_bore_18_selectedOH;
    logic [7:0] boreChildrenBd_bore_18_array;
    logic sigFromSrams_bore_ram_hold;
    logic sigFromSrams_bore_ram_bypass;
    logic sigFromSrams_bore_ram_bp_clken;
    logic sigFromSrams_bore_ram_aux_clk;
    logic sigFromSrams_bore_ram_aux_ckbp;
    logic sigFromSrams_bore_ram_mcp_hold;
    logic sigFromSrams_bore_cgen;
    logic sigFromSrams_bore_1_ram_hold;
    logic sigFromSrams_bore_1_ram_bypass;
    logic sigFromSrams_bore_1_ram_bp_clken;
    logic sigFromSrams_bore_1_ram_aux_clk;
    logic sigFromSrams_bore_1_ram_aux_ckbp;
    logic sigFromSrams_bore_1_ram_mcp_hold;
    logic sigFromSrams_bore_1_cgen;
    logic sigFromSrams_bore_2_ram_hold;
    logic sigFromSrams_bore_2_ram_bypass;
    logic sigFromSrams_bore_2_ram_bp_clken;
    logic sigFromSrams_bore_2_ram_aux_clk;
    logic sigFromSrams_bore_2_ram_aux_ckbp;
    logic sigFromSrams_bore_2_ram_mcp_hold;
    logic sigFromSrams_bore_2_cgen;
    logic sigFromSrams_bore_3_ram_hold;
    logic sigFromSrams_bore_3_ram_bypass;
    logic sigFromSrams_bore_3_ram_bp_clken;
    logic sigFromSrams_bore_3_ram_aux_clk;
    logic sigFromSrams_bore_3_ram_aux_ckbp;
    logic sigFromSrams_bore_3_ram_mcp_hold;
    logic sigFromSrams_bore_3_cgen;
    logic sigFromSrams_bore_4_ram_hold;
    logic sigFromSrams_bore_4_ram_bypass;
    logic sigFromSrams_bore_4_ram_bp_clken;
    logic sigFromSrams_bore_4_ram_aux_clk;
    logic sigFromSrams_bore_4_ram_aux_ckbp;
    logic sigFromSrams_bore_4_ram_mcp_hold;
    logic sigFromSrams_bore_4_cgen;
    logic sigFromSrams_bore_5_ram_hold;
    logic sigFromSrams_bore_5_ram_bypass;
    logic sigFromSrams_bore_5_ram_bp_clken;
    logic sigFromSrams_bore_5_ram_aux_clk;
    logic sigFromSrams_bore_5_ram_aux_ckbp;
    logic sigFromSrams_bore_5_ram_mcp_hold;
    logic sigFromSrams_bore_5_cgen;
    logic sigFromSrams_bore_6_ram_hold;
    logic sigFromSrams_bore_6_ram_bypass;
    logic sigFromSrams_bore_6_ram_bp_clken;
    logic sigFromSrams_bore_6_ram_aux_clk;
    logic sigFromSrams_bore_6_ram_aux_ckbp;
    logic sigFromSrams_bore_6_ram_mcp_hold;
    logic sigFromSrams_bore_6_cgen;
    logic sigFromSrams_bore_7_ram_hold;
    logic sigFromSrams_bore_7_ram_bypass;
    logic sigFromSrams_bore_7_ram_bp_clken;
    logic sigFromSrams_bore_7_ram_aux_clk;
    logic sigFromSrams_bore_7_ram_aux_ckbp;
    logic sigFromSrams_bore_7_ram_mcp_hold;
    logic sigFromSrams_bore_7_cgen;
    logic sigFromSrams_bore_8_ram_hold;
    logic sigFromSrams_bore_8_ram_bypass;
    logic sigFromSrams_bore_8_ram_bp_clken;
    logic sigFromSrams_bore_8_ram_aux_clk;
    logic sigFromSrams_bore_8_ram_aux_ckbp;
    logic sigFromSrams_bore_8_ram_mcp_hold;
    logic sigFromSrams_bore_8_cgen;
    logic sigFromSrams_bore_9_ram_hold;
    logic sigFromSrams_bore_9_ram_bypass;
    logic sigFromSrams_bore_9_ram_bp_clken;
    logic sigFromSrams_bore_9_ram_aux_clk;
    logic sigFromSrams_bore_9_ram_aux_ckbp;
    logic sigFromSrams_bore_9_ram_mcp_hold;
    logic sigFromSrams_bore_9_cgen;
    logic sigFromSrams_bore_10_ram_hold;
    logic sigFromSrams_bore_10_ram_bypass;
    logic sigFromSrams_bore_10_ram_bp_clken;
    logic sigFromSrams_bore_10_ram_aux_clk;
    logic sigFromSrams_bore_10_ram_aux_ckbp;
    logic sigFromSrams_bore_10_ram_mcp_hold;
    logic sigFromSrams_bore_10_cgen;
    logic sigFromSrams_bore_11_ram_hold;
    logic sigFromSrams_bore_11_ram_bypass;
    logic sigFromSrams_bore_11_ram_bp_clken;
    logic sigFromSrams_bore_11_ram_aux_clk;
    logic sigFromSrams_bore_11_ram_aux_ckbp;
    logic sigFromSrams_bore_11_ram_mcp_hold;
    logic sigFromSrams_bore_11_cgen;
    logic sigFromSrams_bore_12_ram_hold;
    logic sigFromSrams_bore_12_ram_bypass;
    logic sigFromSrams_bore_12_ram_bp_clken;
    logic sigFromSrams_bore_12_ram_aux_clk;
    logic sigFromSrams_bore_12_ram_aux_ckbp;
    logic sigFromSrams_bore_12_ram_mcp_hold;
    logic sigFromSrams_bore_12_cgen;
    logic sigFromSrams_bore_13_ram_hold;
    logic sigFromSrams_bore_13_ram_bypass;
    logic sigFromSrams_bore_13_ram_bp_clken;
    logic sigFromSrams_bore_13_ram_aux_clk;
    logic sigFromSrams_bore_13_ram_aux_ckbp;
    logic sigFromSrams_bore_13_ram_mcp_hold;
    logic sigFromSrams_bore_13_cgen;
    logic sigFromSrams_bore_14_ram_hold;
    logic sigFromSrams_bore_14_ram_bypass;
    logic sigFromSrams_bore_14_ram_bp_clken;
    logic sigFromSrams_bore_14_ram_aux_clk;
    logic sigFromSrams_bore_14_ram_aux_ckbp;
    logic sigFromSrams_bore_14_ram_mcp_hold;
    logic sigFromSrams_bore_14_cgen;
    logic sigFromSrams_bore_15_ram_hold;
    logic sigFromSrams_bore_15_ram_bypass;
    logic sigFromSrams_bore_15_ram_bp_clken;
    logic sigFromSrams_bore_15_ram_aux_clk;
    logic sigFromSrams_bore_15_ram_aux_ckbp;
    logic sigFromSrams_bore_15_ram_mcp_hold;
    logic sigFromSrams_bore_15_cgen;
    logic sigFromSrams_bore_16_ram_hold;
    logic sigFromSrams_bore_16_ram_bypass;
    logic sigFromSrams_bore_16_ram_bp_clken;
    logic sigFromSrams_bore_16_ram_aux_clk;
    logic sigFromSrams_bore_16_ram_aux_ckbp;
    logic sigFromSrams_bore_16_ram_mcp_hold;
    logic sigFromSrams_bore_16_cgen;
    logic sigFromSrams_bore_17_ram_hold;
    logic sigFromSrams_bore_17_ram_bypass;
    logic sigFromSrams_bore_17_ram_bp_clken;
    logic sigFromSrams_bore_17_ram_aux_clk;
    logic sigFromSrams_bore_17_ram_aux_ckbp;
    logic sigFromSrams_bore_17_ram_mcp_hold;
    logic sigFromSrams_bore_17_cgen;
    logic sigFromSrams_bore_18_ram_hold;
    logic sigFromSrams_bore_18_ram_bypass;
    logic sigFromSrams_bore_18_ram_bp_clken;
    logic sigFromSrams_bore_18_ram_aux_clk;
    logic sigFromSrams_bore_18_ram_aux_ckbp;
    logic sigFromSrams_bore_18_ram_mcp_hold;
    logic sigFromSrams_bore_18_cgen;
    logic sigFromSrams_bore_19_ram_hold;
    logic sigFromSrams_bore_19_ram_bypass;
    logic sigFromSrams_bore_19_ram_bp_clken;
    logic sigFromSrams_bore_19_ram_aux_clk;
    logic sigFromSrams_bore_19_ram_aux_ckbp;
    logic sigFromSrams_bore_19_ram_mcp_hold;
    logic sigFromSrams_bore_19_cgen;
    logic sigFromSrams_bore_20_ram_hold;
    logic sigFromSrams_bore_20_ram_bypass;
    logic sigFromSrams_bore_20_ram_bp_clken;
    logic sigFromSrams_bore_20_ram_aux_clk;
    logic sigFromSrams_bore_20_ram_aux_ckbp;
    logic sigFromSrams_bore_20_ram_mcp_hold;
    logic sigFromSrams_bore_20_cgen;
    logic sigFromSrams_bore_21_ram_hold;
    logic sigFromSrams_bore_21_ram_bypass;
    logic sigFromSrams_bore_21_ram_bp_clken;
    logic sigFromSrams_bore_21_ram_aux_clk;
    logic sigFromSrams_bore_21_ram_aux_ckbp;
    logic sigFromSrams_bore_21_ram_mcp_hold;
    logic sigFromSrams_bore_21_cgen;
    logic sigFromSrams_bore_22_ram_hold;
    logic sigFromSrams_bore_22_ram_bypass;
    logic sigFromSrams_bore_22_ram_bp_clken;
    logic sigFromSrams_bore_22_ram_aux_clk;
    logic sigFromSrams_bore_22_ram_aux_ckbp;
    logic sigFromSrams_bore_22_ram_mcp_hold;
    logic sigFromSrams_bore_22_cgen;
    logic sigFromSrams_bore_23_ram_hold;
    logic sigFromSrams_bore_23_ram_bypass;
    logic sigFromSrams_bore_23_ram_bp_clken;
    logic sigFromSrams_bore_23_ram_aux_clk;
    logic sigFromSrams_bore_23_ram_aux_ckbp;
    logic sigFromSrams_bore_23_ram_mcp_hold;
    logic sigFromSrams_bore_23_cgen;
    logic sigFromSrams_bore_24_ram_hold;
    logic sigFromSrams_bore_24_ram_bypass;
    logic sigFromSrams_bore_24_ram_bp_clken;
    logic sigFromSrams_bore_24_ram_aux_clk;
    logic sigFromSrams_bore_24_ram_aux_ckbp;
    logic sigFromSrams_bore_24_ram_mcp_hold;
    logic sigFromSrams_bore_24_cgen;
    logic sigFromSrams_bore_25_ram_hold;
    logic sigFromSrams_bore_25_ram_bypass;
    logic sigFromSrams_bore_25_ram_bp_clken;
    logic sigFromSrams_bore_25_ram_aux_clk;
    logic sigFromSrams_bore_25_ram_aux_ckbp;
    logic sigFromSrams_bore_25_ram_mcp_hold;
    logic sigFromSrams_bore_25_cgen;
    logic sigFromSrams_bore_26_ram_hold;
    logic sigFromSrams_bore_26_ram_bypass;
    logic sigFromSrams_bore_26_ram_bp_clken;
    logic sigFromSrams_bore_26_ram_aux_clk;
    logic sigFromSrams_bore_26_ram_aux_ckbp;
    logic sigFromSrams_bore_26_ram_mcp_hold;
    logic sigFromSrams_bore_26_cgen;
    logic sigFromSrams_bore_27_ram_hold;
    logic sigFromSrams_bore_27_ram_bypass;
    logic sigFromSrams_bore_27_ram_bp_clken;
    logic sigFromSrams_bore_27_ram_aux_clk;
    logic sigFromSrams_bore_27_ram_aux_ckbp;
    logic sigFromSrams_bore_27_ram_mcp_hold;
    logic sigFromSrams_bore_27_cgen;
    logic sigFromSrams_bore_28_ram_hold;
    logic sigFromSrams_bore_28_ram_bypass;
    logic sigFromSrams_bore_28_ram_bp_clken;
    logic sigFromSrams_bore_28_ram_aux_clk;
    logic sigFromSrams_bore_28_ram_aux_ckbp;
    logic sigFromSrams_bore_28_ram_mcp_hold;
    logic sigFromSrams_bore_28_cgen;
    logic sigFromSrams_bore_29_ram_hold;
    logic sigFromSrams_bore_29_ram_bypass;
    logic sigFromSrams_bore_29_ram_bp_clken;
    logic sigFromSrams_bore_29_ram_aux_clk;
    logic sigFromSrams_bore_29_ram_aux_ckbp;
    logic sigFromSrams_bore_29_ram_mcp_hold;
    logic sigFromSrams_bore_29_cgen;
    logic sigFromSrams_bore_30_ram_hold;
    logic sigFromSrams_bore_30_ram_bypass;
    logic sigFromSrams_bore_30_ram_bp_clken;
    logic sigFromSrams_bore_30_ram_aux_clk;
    logic sigFromSrams_bore_30_ram_aux_ckbp;
    logic sigFromSrams_bore_30_ram_mcp_hold;
    logic sigFromSrams_bore_30_cgen;
    logic sigFromSrams_bore_31_ram_hold;
    logic sigFromSrams_bore_31_ram_bypass;
    logic sigFromSrams_bore_31_ram_bp_clken;
    logic sigFromSrams_bore_31_ram_aux_clk;
    logic sigFromSrams_bore_31_ram_aux_ckbp;
    logic sigFromSrams_bore_31_ram_mcp_hold;
    logic sigFromSrams_bore_31_cgen;
    logic sigFromSrams_bore_32_ram_hold;
    logic sigFromSrams_bore_32_ram_bypass;
    logic sigFromSrams_bore_32_ram_bp_clken;
    logic sigFromSrams_bore_32_ram_aux_clk;
    logic sigFromSrams_bore_32_ram_aux_ckbp;
    logic sigFromSrams_bore_32_ram_mcp_hold;
    logic sigFromSrams_bore_32_cgen;
    logic sigFromSrams_bore_33_ram_hold;
    logic sigFromSrams_bore_33_ram_bypass;
    logic sigFromSrams_bore_33_ram_bp_clken;
    logic sigFromSrams_bore_33_ram_aux_clk;
    logic sigFromSrams_bore_33_ram_aux_ckbp;
    logic sigFromSrams_bore_33_ram_mcp_hold;
    logic sigFromSrams_bore_33_cgen;
    logic sigFromSrams_bore_34_ram_hold;
    logic sigFromSrams_bore_34_ram_bypass;
    logic sigFromSrams_bore_34_ram_bp_clken;
    logic sigFromSrams_bore_34_ram_aux_clk;
    logic sigFromSrams_bore_34_ram_aux_ckbp;
    logic sigFromSrams_bore_34_ram_mcp_hold;
    logic sigFromSrams_bore_34_cgen;
    logic sigFromSrams_bore_35_ram_hold;
    logic sigFromSrams_bore_35_ram_bypass;
    logic sigFromSrams_bore_35_ram_bp_clken;
    logic sigFromSrams_bore_35_ram_aux_clk;
    logic sigFromSrams_bore_35_ram_aux_ckbp;
    logic sigFromSrams_bore_35_ram_mcp_hold;
    logic sigFromSrams_bore_35_cgen;
    logic sigFromSrams_bore_36_ram_hold;
    logic sigFromSrams_bore_36_ram_bypass;
    logic sigFromSrams_bore_36_ram_bp_clken;
    logic sigFromSrams_bore_36_ram_aux_clk;
    logic sigFromSrams_bore_36_ram_aux_ckbp;
    logic sigFromSrams_bore_36_ram_mcp_hold;
    logic sigFromSrams_bore_36_cgen;
    logic sigFromSrams_bore_37_ram_hold;
    logic sigFromSrams_bore_37_ram_bypass;
    logic sigFromSrams_bore_37_ram_bp_clken;
    logic sigFromSrams_bore_37_ram_aux_clk;
    logic sigFromSrams_bore_37_ram_aux_ckbp;
    logic sigFromSrams_bore_37_ram_mcp_hold;
    logic sigFromSrams_bore_37_cgen;
    logic sigFromSrams_bore_38_ram_hold;
    logic sigFromSrams_bore_38_ram_bypass;
    logic sigFromSrams_bore_38_ram_bp_clken;
    logic sigFromSrams_bore_38_ram_aux_clk;
    logic sigFromSrams_bore_38_ram_aux_ckbp;
    logic sigFromSrams_bore_38_ram_mcp_hold;
    logic sigFromSrams_bore_38_cgen;
    logic sigFromSrams_bore_39_ram_hold;
    logic sigFromSrams_bore_39_ram_bypass;
    logic sigFromSrams_bore_39_ram_bp_clken;
    logic sigFromSrams_bore_39_ram_aux_clk;
    logic sigFromSrams_bore_39_ram_aux_ckbp;
    logic sigFromSrams_bore_39_ram_mcp_hold;
    logic sigFromSrams_bore_39_cgen;
    logic sigFromSrams_bore_40_ram_hold;
    logic sigFromSrams_bore_40_ram_bypass;
    logic sigFromSrams_bore_40_ram_bp_clken;
    logic sigFromSrams_bore_40_ram_aux_clk;
    logic sigFromSrams_bore_40_ram_aux_ckbp;
    logic sigFromSrams_bore_40_ram_mcp_hold;
    logic sigFromSrams_bore_40_cgen;
    logic sigFromSrams_bore_41_ram_hold;
    logic sigFromSrams_bore_41_ram_bypass;
    logic sigFromSrams_bore_41_ram_bp_clken;
    logic sigFromSrams_bore_41_ram_aux_clk;
    logic sigFromSrams_bore_41_ram_aux_ckbp;
    logic sigFromSrams_bore_41_ram_mcp_hold;
    logic sigFromSrams_bore_41_cgen;
    logic sigFromSrams_bore_42_ram_hold;
    logic sigFromSrams_bore_42_ram_bypass;
    logic sigFromSrams_bore_42_ram_bp_clken;
    logic sigFromSrams_bore_42_ram_aux_clk;
    logic sigFromSrams_bore_42_ram_aux_ckbp;
    logic sigFromSrams_bore_42_ram_mcp_hold;
    logic sigFromSrams_bore_42_cgen;
    logic sigFromSrams_bore_43_ram_hold;
    logic sigFromSrams_bore_43_ram_bypass;
    logic sigFromSrams_bore_43_ram_bp_clken;
    logic sigFromSrams_bore_43_ram_aux_clk;
    logic sigFromSrams_bore_43_ram_aux_ckbp;
    logic sigFromSrams_bore_43_ram_mcp_hold;
    logic sigFromSrams_bore_43_cgen;
    logic sigFromSrams_bore_44_ram_hold;
    logic sigFromSrams_bore_44_ram_bypass;
    logic sigFromSrams_bore_44_ram_bp_clken;
    logic sigFromSrams_bore_44_ram_aux_clk;
    logic sigFromSrams_bore_44_ram_aux_ckbp;
    logic sigFromSrams_bore_44_ram_mcp_hold;
    logic sigFromSrams_bore_44_cgen;
    logic sigFromSrams_bore_45_ram_hold;
    logic sigFromSrams_bore_45_ram_bypass;
    logic sigFromSrams_bore_45_ram_bp_clken;
    logic sigFromSrams_bore_45_ram_aux_clk;
    logic sigFromSrams_bore_45_ram_aux_ckbp;
    logic sigFromSrams_bore_45_ram_mcp_hold;
    logic sigFromSrams_bore_45_cgen;
    logic sigFromSrams_bore_46_ram_hold;
    logic sigFromSrams_bore_46_ram_bypass;
    logic sigFromSrams_bore_46_ram_bp_clken;
    logic sigFromSrams_bore_46_ram_aux_clk;
    logic sigFromSrams_bore_46_ram_aux_ckbp;
    logic sigFromSrams_bore_46_ram_mcp_hold;
    logic sigFromSrams_bore_46_cgen;
    logic sigFromSrams_bore_47_ram_hold;
    logic sigFromSrams_bore_47_ram_bypass;
    logic sigFromSrams_bore_47_ram_bp_clken;
    logic sigFromSrams_bore_47_ram_aux_clk;
    logic sigFromSrams_bore_47_ram_aux_ckbp;
    logic sigFromSrams_bore_47_ram_mcp_hold;
    logic sigFromSrams_bore_47_cgen;
    logic sigFromSrams_bore_48_ram_hold;
    logic sigFromSrams_bore_48_ram_bypass;
    logic sigFromSrams_bore_48_ram_bp_clken;
    logic sigFromSrams_bore_48_ram_aux_clk;
    logic sigFromSrams_bore_48_ram_aux_ckbp;
    logic sigFromSrams_bore_48_ram_mcp_hold;
    logic sigFromSrams_bore_48_cgen;
    logic sigFromSrams_bore_49_ram_hold;
    logic sigFromSrams_bore_49_ram_bypass;
    logic sigFromSrams_bore_49_ram_bp_clken;
    logic sigFromSrams_bore_49_ram_aux_clk;
    logic sigFromSrams_bore_49_ram_aux_ckbp;
    logic sigFromSrams_bore_49_ram_mcp_hold;
    logic sigFromSrams_bore_49_cgen;
    logic sigFromSrams_bore_50_ram_hold;
    logic sigFromSrams_bore_50_ram_bypass;
    logic sigFromSrams_bore_50_ram_bp_clken;
    logic sigFromSrams_bore_50_ram_aux_clk;
    logic sigFromSrams_bore_50_ram_aux_ckbp;
    logic sigFromSrams_bore_50_ram_mcp_hold;
    logic sigFromSrams_bore_50_cgen;
    logic sigFromSrams_bore_51_ram_hold;
    logic sigFromSrams_bore_51_ram_bypass;
    logic sigFromSrams_bore_51_ram_bp_clken;
    logic sigFromSrams_bore_51_ram_aux_clk;
    logic sigFromSrams_bore_51_ram_aux_ckbp;
    logic sigFromSrams_bore_51_ram_mcp_hold;
    logic sigFromSrams_bore_51_cgen;
    logic sigFromSrams_bore_52_ram_hold;
    logic sigFromSrams_bore_52_ram_bypass;
    logic sigFromSrams_bore_52_ram_bp_clken;
    logic sigFromSrams_bore_52_ram_aux_clk;
    logic sigFromSrams_bore_52_ram_aux_ckbp;
    logic sigFromSrams_bore_52_ram_mcp_hold;
    logic sigFromSrams_bore_52_cgen;
    logic sigFromSrams_bore_53_ram_hold;
    logic sigFromSrams_bore_53_ram_bypass;
    logic sigFromSrams_bore_53_ram_bp_clken;
    logic sigFromSrams_bore_53_ram_aux_clk;
    logic sigFromSrams_bore_53_ram_aux_ckbp;
    logic sigFromSrams_bore_53_ram_mcp_hold;
    logic sigFromSrams_bore_53_cgen;
    logic sigFromSrams_bore_54_ram_hold;
    logic sigFromSrams_bore_54_ram_bypass;
    logic sigFromSrams_bore_54_ram_bp_clken;
    logic sigFromSrams_bore_54_ram_aux_clk;
    logic sigFromSrams_bore_54_ram_aux_ckbp;
    logic sigFromSrams_bore_54_ram_mcp_hold;
    logic sigFromSrams_bore_54_cgen;
    logic sigFromSrams_bore_55_ram_hold;
    logic sigFromSrams_bore_55_ram_bypass;
    logic sigFromSrams_bore_55_ram_bp_clken;
    logic sigFromSrams_bore_55_ram_aux_clk;
    logic sigFromSrams_bore_55_ram_aux_ckbp;
    logic sigFromSrams_bore_55_ram_mcp_hold;
    logic sigFromSrams_bore_55_cgen;
    logic sigFromSrams_bore_56_ram_hold;
    logic sigFromSrams_bore_56_ram_bypass;
    logic sigFromSrams_bore_56_ram_bp_clken;
    logic sigFromSrams_bore_56_ram_aux_clk;
    logic sigFromSrams_bore_56_ram_aux_ckbp;
    logic sigFromSrams_bore_56_ram_mcp_hold;
    logic sigFromSrams_bore_56_cgen;
    logic sigFromSrams_bore_57_ram_hold;
    logic sigFromSrams_bore_57_ram_bypass;
    logic sigFromSrams_bore_57_ram_bp_clken;
    logic sigFromSrams_bore_57_ram_aux_clk;
    logic sigFromSrams_bore_57_ram_aux_ckbp;
    logic sigFromSrams_bore_57_ram_mcp_hold;
    logic sigFromSrams_bore_57_cgen;
    logic sigFromSrams_bore_58_ram_hold;
    logic sigFromSrams_bore_58_ram_bypass;
    logic sigFromSrams_bore_58_ram_bp_clken;
    logic sigFromSrams_bore_58_ram_aux_clk;
    logic sigFromSrams_bore_58_ram_aux_ckbp;
    logic sigFromSrams_bore_58_ram_mcp_hold;
    logic sigFromSrams_bore_58_cgen;
    logic sigFromSrams_bore_59_ram_hold;
    logic sigFromSrams_bore_59_ram_bypass;
    logic sigFromSrams_bore_59_ram_bp_clken;
    logic sigFromSrams_bore_59_ram_aux_clk;
    logic sigFromSrams_bore_59_ram_aux_ckbp;
    logic sigFromSrams_bore_59_ram_mcp_hold;
    logic sigFromSrams_bore_59_cgen;
    logic sigFromSrams_bore_60_ram_hold;
    logic sigFromSrams_bore_60_ram_bypass;
    logic sigFromSrams_bore_60_ram_bp_clken;
    logic sigFromSrams_bore_60_ram_aux_clk;
    logic sigFromSrams_bore_60_ram_aux_ckbp;
    logic sigFromSrams_bore_60_ram_mcp_hold;
    logic sigFromSrams_bore_60_cgen;
    logic sigFromSrams_bore_61_ram_hold;
    logic sigFromSrams_bore_61_ram_bypass;
    logic sigFromSrams_bore_61_ram_bp_clken;
    logic sigFromSrams_bore_61_ram_aux_clk;
    logic sigFromSrams_bore_61_ram_aux_ckbp;
    logic sigFromSrams_bore_61_ram_mcp_hold;
    logic sigFromSrams_bore_61_cgen;
    logic sigFromSrams_bore_62_ram_hold;
    logic sigFromSrams_bore_62_ram_bypass;
    logic sigFromSrams_bore_62_ram_bp_clken;
    logic sigFromSrams_bore_62_ram_aux_clk;
    logic sigFromSrams_bore_62_ram_aux_ckbp;
    logic sigFromSrams_bore_62_ram_mcp_hold;
    logic sigFromSrams_bore_62_cgen;
    logic sigFromSrams_bore_63_ram_hold;
    logic sigFromSrams_bore_63_ram_bypass;
    logic sigFromSrams_bore_63_ram_bp_clken;
    logic sigFromSrams_bore_63_ram_aux_clk;
    logic sigFromSrams_bore_63_ram_aux_ckbp;
    logic sigFromSrams_bore_63_ram_mcp_hold;
    logic sigFromSrams_bore_63_cgen;
    logic sigFromSrams_bore_64_ram_hold;
    logic sigFromSrams_bore_64_ram_bypass;
    logic sigFromSrams_bore_64_ram_bp_clken;
    logic sigFromSrams_bore_64_ram_aux_clk;
    logic sigFromSrams_bore_64_ram_aux_ckbp;
    logic sigFromSrams_bore_64_ram_mcp_hold;
    logic sigFromSrams_bore_64_cgen;
    logic sigFromSrams_bore_65_ram_hold;
    logic sigFromSrams_bore_65_ram_bypass;
    logic sigFromSrams_bore_65_ram_bp_clken;
    logic sigFromSrams_bore_65_ram_aux_clk;
    logic sigFromSrams_bore_65_ram_aux_ckbp;
    logic sigFromSrams_bore_65_ram_mcp_hold;
    logic sigFromSrams_bore_65_cgen;
    logic sigFromSrams_bore_66_ram_hold;
    logic sigFromSrams_bore_66_ram_bypass;
    logic sigFromSrams_bore_66_ram_bp_clken;
    logic sigFromSrams_bore_66_ram_aux_clk;
    logic sigFromSrams_bore_66_ram_aux_ckbp;
    logic sigFromSrams_bore_66_ram_mcp_hold;
    logic sigFromSrams_bore_66_cgen;
    logic sigFromSrams_bore_67_ram_hold;
    logic sigFromSrams_bore_67_ram_bypass;
    logic sigFromSrams_bore_67_ram_bp_clken;
    logic sigFromSrams_bore_67_ram_aux_clk;
    logic sigFromSrams_bore_67_ram_aux_ckbp;
    logic sigFromSrams_bore_67_ram_mcp_hold;
    logic sigFromSrams_bore_67_cgen;
    logic sigFromSrams_bore_68_ram_hold;
    logic sigFromSrams_bore_68_ram_bypass;
    logic sigFromSrams_bore_68_ram_bp_clken;
    logic sigFromSrams_bore_68_ram_aux_clk;
    logic sigFromSrams_bore_68_ram_aux_ckbp;
    logic sigFromSrams_bore_68_ram_mcp_hold;
    logic sigFromSrams_bore_68_cgen;
    logic sigFromSrams_bore_69_ram_hold;
    logic sigFromSrams_bore_69_ram_bypass;
    logic sigFromSrams_bore_69_ram_bp_clken;
    logic sigFromSrams_bore_69_ram_aux_clk;
    logic sigFromSrams_bore_69_ram_aux_ckbp;
    logic sigFromSrams_bore_69_ram_mcp_hold;
    logic sigFromSrams_bore_69_cgen;
    logic sigFromSrams_bore_70_ram_hold;
    logic sigFromSrams_bore_70_ram_bypass;
    logic sigFromSrams_bore_70_ram_bp_clken;
    logic sigFromSrams_bore_70_ram_aux_clk;
    logic sigFromSrams_bore_70_ram_aux_ckbp;
    logic sigFromSrams_bore_70_ram_mcp_hold;
    logic sigFromSrams_bore_70_cgen;
    logic sigFromSrams_bore_71_ram_hold;
    logic sigFromSrams_bore_71_ram_bypass;
    logic sigFromSrams_bore_71_ram_bp_clken;
    logic sigFromSrams_bore_71_ram_aux_clk;
    logic sigFromSrams_bore_71_ram_aux_ckbp;
    logic sigFromSrams_bore_71_ram_mcp_hold;
    logic sigFromSrams_bore_71_cgen;
    logic sigFromSrams_bore_72_ram_hold;
    logic sigFromSrams_bore_72_ram_bypass;
    logic sigFromSrams_bore_72_ram_bp_clken;
    logic sigFromSrams_bore_72_ram_aux_clk;
    logic sigFromSrams_bore_72_ram_aux_ckbp;
    logic sigFromSrams_bore_72_ram_mcp_hold;
    logic sigFromSrams_bore_72_cgen;
    logic sigFromSrams_bore_73_ram_hold;
    logic sigFromSrams_bore_73_ram_bypass;
    logic sigFromSrams_bore_73_ram_bp_clken;
    logic sigFromSrams_bore_73_ram_aux_clk;
    logic sigFromSrams_bore_73_ram_aux_ckbp;
    logic sigFromSrams_bore_73_ram_mcp_hold;
    logic sigFromSrams_bore_73_cgen;
    logic sigFromSrams_bore_74_ram_hold;
    logic sigFromSrams_bore_74_ram_bypass;
    logic sigFromSrams_bore_74_ram_bp_clken;
    logic sigFromSrams_bore_74_ram_aux_clk;
    logic sigFromSrams_bore_74_ram_aux_ckbp;
    logic sigFromSrams_bore_74_ram_mcp_hold;
    logic sigFromSrams_bore_74_cgen;
    logic sigFromSrams_bore_75_ram_hold;
    logic sigFromSrams_bore_75_ram_bypass;
    logic sigFromSrams_bore_75_ram_bp_clken;
    logic sigFromSrams_bore_75_ram_aux_clk;
    logic sigFromSrams_bore_75_ram_aux_ckbp;
    logic sigFromSrams_bore_75_ram_mcp_hold;
    logic sigFromSrams_bore_75_cgen;
    logic sigFromSrams_bore_76_ram_hold;
    logic sigFromSrams_bore_76_ram_bypass;
    logic sigFromSrams_bore_76_ram_bp_clken;
    logic sigFromSrams_bore_76_ram_aux_clk;
    logic sigFromSrams_bore_76_ram_aux_ckbp;
    logic sigFromSrams_bore_76_ram_mcp_hold;
    logic sigFromSrams_bore_76_cgen;
    logic sigFromSrams_bore_77_ram_hold;
    logic sigFromSrams_bore_77_ram_bypass;
    logic sigFromSrams_bore_77_ram_bp_clken;
    logic sigFromSrams_bore_77_ram_aux_clk;
    logic sigFromSrams_bore_77_ram_aux_ckbp;
    logic sigFromSrams_bore_77_ram_mcp_hold;
    logic sigFromSrams_bore_77_cgen;
    logic sigFromSrams_bore_78_ram_hold;
    logic sigFromSrams_bore_78_ram_bypass;
    logic sigFromSrams_bore_78_ram_bp_clken;
    logic sigFromSrams_bore_78_ram_aux_clk;
    logic sigFromSrams_bore_78_ram_aux_ckbp;
    logic sigFromSrams_bore_78_ram_mcp_hold;
    logic sigFromSrams_bore_78_cgen;
    logic sigFromSrams_bore_79_ram_hold;
    logic sigFromSrams_bore_79_ram_bypass;
    logic sigFromSrams_bore_79_ram_bp_clken;
    logic sigFromSrams_bore_79_ram_aux_clk;
    logic sigFromSrams_bore_79_ram_aux_ckbp;
    logic sigFromSrams_bore_79_ram_mcp_hold;
    logic sigFromSrams_bore_79_cgen;
    logic sigFromSrams_bore_80_ram_hold;
    logic sigFromSrams_bore_80_ram_bypass;
    logic sigFromSrams_bore_80_ram_bp_clken;
    logic sigFromSrams_bore_80_ram_aux_clk;
    logic sigFromSrams_bore_80_ram_aux_ckbp;
    logic sigFromSrams_bore_80_ram_mcp_hold;
    logic sigFromSrams_bore_80_cgen;
    logic sigFromSrams_bore_81_ram_hold;
    logic sigFromSrams_bore_81_ram_bypass;
    logic sigFromSrams_bore_81_ram_bp_clken;
    logic sigFromSrams_bore_81_ram_aux_clk;
    logic sigFromSrams_bore_81_ram_aux_ckbp;
    logic sigFromSrams_bore_81_ram_mcp_hold;
    logic sigFromSrams_bore_81_cgen;
    logic sigFromSrams_bore_82_ram_hold;
    logic sigFromSrams_bore_82_ram_bypass;
    logic sigFromSrams_bore_82_ram_bp_clken;
    logic sigFromSrams_bore_82_ram_aux_clk;
    logic sigFromSrams_bore_82_ram_aux_ckbp;
    logic sigFromSrams_bore_82_ram_mcp_hold;
    logic sigFromSrams_bore_82_cgen;
    logic sigFromSrams_bore_83_ram_hold;
    logic sigFromSrams_bore_83_ram_bypass;
    logic sigFromSrams_bore_83_ram_bp_clken;
    logic sigFromSrams_bore_83_ram_aux_clk;
    logic sigFromSrams_bore_83_ram_aux_ckbp;
    logic sigFromSrams_bore_83_ram_mcp_hold;
    logic sigFromSrams_bore_83_cgen;
    logic sigFromSrams_bore_84_ram_hold;
    logic sigFromSrams_bore_84_ram_bypass;
    logic sigFromSrams_bore_84_ram_bp_clken;
    logic sigFromSrams_bore_84_ram_aux_clk;
    logic sigFromSrams_bore_84_ram_aux_ckbp;
    logic sigFromSrams_bore_84_ram_mcp_hold;
    logic sigFromSrams_bore_84_cgen;
    logic sigFromSrams_bore_85_ram_hold;
    logic sigFromSrams_bore_85_ram_bypass;
    logic sigFromSrams_bore_85_ram_bp_clken;
    logic sigFromSrams_bore_85_ram_aux_clk;
    logic sigFromSrams_bore_85_ram_aux_ckbp;
    logic sigFromSrams_bore_85_ram_mcp_hold;
    logic sigFromSrams_bore_85_cgen;
    logic sigFromSrams_bore_86_ram_hold;
    logic sigFromSrams_bore_86_ram_bypass;
    logic sigFromSrams_bore_86_ram_bp_clken;
    logic sigFromSrams_bore_86_ram_aux_clk;
    logic sigFromSrams_bore_86_ram_aux_ckbp;
    logic sigFromSrams_bore_86_ram_mcp_hold;
    logic sigFromSrams_bore_86_cgen;
    logic sigFromSrams_bore_87_ram_hold;
    logic sigFromSrams_bore_87_ram_bypass;
    logic sigFromSrams_bore_87_ram_bp_clken;
    logic sigFromSrams_bore_87_ram_aux_clk;
    logic sigFromSrams_bore_87_ram_aux_ckbp;
    logic sigFromSrams_bore_87_ram_mcp_hold;
    logic sigFromSrams_bore_87_cgen;
    logic sigFromSrams_bore_88_ram_hold;
    logic sigFromSrams_bore_88_ram_bypass;
    logic sigFromSrams_bore_88_ram_bp_clken;
    logic sigFromSrams_bore_88_ram_aux_clk;
    logic sigFromSrams_bore_88_ram_aux_ckbp;
    logic sigFromSrams_bore_88_ram_mcp_hold;
    logic sigFromSrams_bore_88_cgen;
    logic sigFromSrams_bore_89_ram_hold;
    logic sigFromSrams_bore_89_ram_bypass;
    logic sigFromSrams_bore_89_ram_bp_clken;
    logic sigFromSrams_bore_89_ram_aux_clk;
    logic sigFromSrams_bore_89_ram_aux_ckbp;
    logic sigFromSrams_bore_89_ram_mcp_hold;
    logic sigFromSrams_bore_89_cgen;
    logic sigFromSrams_bore_90_ram_hold;
    logic sigFromSrams_bore_90_ram_bypass;
    logic sigFromSrams_bore_90_ram_bp_clken;
    logic sigFromSrams_bore_90_ram_aux_clk;
    logic sigFromSrams_bore_90_ram_aux_ckbp;
    logic sigFromSrams_bore_90_ram_mcp_hold;
    logic sigFromSrams_bore_90_cgen;
    logic sigFromSrams_bore_91_ram_hold;
    logic sigFromSrams_bore_91_ram_bypass;
    logic sigFromSrams_bore_91_ram_bp_clken;
    logic sigFromSrams_bore_91_ram_aux_clk;
    logic sigFromSrams_bore_91_ram_aux_ckbp;
    logic sigFromSrams_bore_91_ram_mcp_hold;
    logic sigFromSrams_bore_91_cgen;
    logic sigFromSrams_bore_92_ram_hold;
    logic sigFromSrams_bore_92_ram_bypass;
    logic sigFromSrams_bore_92_ram_bp_clken;
    logic sigFromSrams_bore_92_ram_aux_clk;
    logic sigFromSrams_bore_92_ram_aux_ckbp;
    logic sigFromSrams_bore_92_ram_mcp_hold;
    logic sigFromSrams_bore_92_cgen;
    logic sigFromSrams_bore_93_ram_hold;
    logic sigFromSrams_bore_93_ram_bypass;
    logic sigFromSrams_bore_93_ram_bp_clken;
    logic sigFromSrams_bore_93_ram_aux_clk;
    logic sigFromSrams_bore_93_ram_aux_ckbp;
    logic sigFromSrams_bore_93_ram_mcp_hold;
    logic sigFromSrams_bore_93_cgen;
    logic sigFromSrams_bore_94_ram_hold;
    logic sigFromSrams_bore_94_ram_bypass;
    logic sigFromSrams_bore_94_ram_bp_clken;
    logic sigFromSrams_bore_94_ram_aux_clk;
    logic sigFromSrams_bore_94_ram_aux_ckbp;
    logic sigFromSrams_bore_94_ram_mcp_hold;
    logic sigFromSrams_bore_94_cgen;
    logic sigFromSrams_bore_95_ram_hold;
    logic sigFromSrams_bore_95_ram_bypass;
    logic sigFromSrams_bore_95_ram_bp_clken;
    logic sigFromSrams_bore_95_ram_aux_clk;
    logic sigFromSrams_bore_95_ram_aux_ckbp;
    logic sigFromSrams_bore_95_ram_mcp_hold;
    logic sigFromSrams_bore_95_cgen;
    logic sigFromSrams_bore_96_ram_hold;
    logic sigFromSrams_bore_96_ram_bypass;
    logic sigFromSrams_bore_96_ram_bp_clken;
    logic sigFromSrams_bore_96_ram_aux_clk;
    logic sigFromSrams_bore_96_ram_aux_ckbp;
    logic sigFromSrams_bore_96_ram_mcp_hold;
    logic sigFromSrams_bore_96_cgen;
    logic sigFromSrams_bore_97_ram_hold;
    logic sigFromSrams_bore_97_ram_bypass;
    logic sigFromSrams_bore_97_ram_bp_clken;
    logic sigFromSrams_bore_97_ram_aux_clk;
    logic sigFromSrams_bore_97_ram_aux_ckbp;
    logic sigFromSrams_bore_97_ram_mcp_hold;
    logic sigFromSrams_bore_97_cgen;
    logic sigFromSrams_bore_98_ram_hold;
    logic sigFromSrams_bore_98_ram_bypass;
    logic sigFromSrams_bore_98_ram_bp_clken;
    logic sigFromSrams_bore_98_ram_aux_clk;
    logic sigFromSrams_bore_98_ram_aux_ckbp;
    logic sigFromSrams_bore_98_ram_mcp_hold;
    logic sigFromSrams_bore_98_cgen;
    logic sigFromSrams_bore_99_ram_hold;
    logic sigFromSrams_bore_99_ram_bypass;
    logic sigFromSrams_bore_99_ram_bp_clken;
    logic sigFromSrams_bore_99_ram_aux_clk;
    logic sigFromSrams_bore_99_ram_aux_ckbp;
    logic sigFromSrams_bore_99_ram_mcp_hold;
    logic sigFromSrams_bore_99_cgen;
    logic sigFromSrams_bore_100_ram_hold;
    logic sigFromSrams_bore_100_ram_bypass;
    logic sigFromSrams_bore_100_ram_bp_clken;
    logic sigFromSrams_bore_100_ram_aux_clk;
    logic sigFromSrams_bore_100_ram_aux_ckbp;
    logic sigFromSrams_bore_100_ram_mcp_hold;
    logic sigFromSrams_bore_100_cgen;
    logic sigFromSrams_bore_101_ram_hold;
    logic sigFromSrams_bore_101_ram_bypass;
    logic sigFromSrams_bore_101_ram_bp_clken;
    logic sigFromSrams_bore_101_ram_aux_clk;
    logic sigFromSrams_bore_101_ram_aux_ckbp;
    logic sigFromSrams_bore_101_ram_mcp_hold;
    logic sigFromSrams_bore_101_cgen;
    logic sigFromSrams_bore_102_ram_hold;
    logic sigFromSrams_bore_102_ram_bypass;
    logic sigFromSrams_bore_102_ram_bp_clken;
    logic sigFromSrams_bore_102_ram_aux_clk;
    logic sigFromSrams_bore_102_ram_aux_ckbp;
    logic sigFromSrams_bore_102_ram_mcp_hold;
    logic sigFromSrams_bore_102_cgen;
    logic sigFromSrams_bore_103_ram_hold;
    logic sigFromSrams_bore_103_ram_bypass;
    logic sigFromSrams_bore_103_ram_bp_clken;
    logic sigFromSrams_bore_103_ram_aux_clk;
    logic sigFromSrams_bore_103_ram_aux_ckbp;
    logic sigFromSrams_bore_103_ram_mcp_hold;
    logic sigFromSrams_bore_103_cgen;
    logic sigFromSrams_bore_104_ram_hold;
    logic sigFromSrams_bore_104_ram_bypass;
    logic sigFromSrams_bore_104_ram_bp_clken;
    logic sigFromSrams_bore_104_ram_aux_clk;
    logic sigFromSrams_bore_104_ram_aux_ckbp;
    logic sigFromSrams_bore_104_ram_mcp_hold;
    logic sigFromSrams_bore_104_cgen;
    logic sigFromSrams_bore_105_ram_hold;
    logic sigFromSrams_bore_105_ram_bypass;
    logic sigFromSrams_bore_105_ram_bp_clken;
    logic sigFromSrams_bore_105_ram_aux_clk;
    logic sigFromSrams_bore_105_ram_aux_ckbp;
    logic sigFromSrams_bore_105_ram_mcp_hold;
    logic sigFromSrams_bore_105_cgen;
    logic sigFromSrams_bore_106_ram_hold;
    logic sigFromSrams_bore_106_ram_bypass;
    logic sigFromSrams_bore_106_ram_bp_clken;
    logic sigFromSrams_bore_106_ram_aux_clk;
    logic sigFromSrams_bore_106_ram_aux_ckbp;
    logic sigFromSrams_bore_106_ram_mcp_hold;
    logic sigFromSrams_bore_106_cgen;
    logic sigFromSrams_bore_107_ram_hold;
    logic sigFromSrams_bore_107_ram_bypass;
    logic sigFromSrams_bore_107_ram_bp_clken;
    logic sigFromSrams_bore_107_ram_aux_clk;
    logic sigFromSrams_bore_107_ram_aux_ckbp;
    logic sigFromSrams_bore_107_ram_mcp_hold;
    logic sigFromSrams_bore_107_cgen;
    logic sigFromSrams_bore_108_ram_hold;
    logic sigFromSrams_bore_108_ram_bypass;
    logic sigFromSrams_bore_108_ram_bp_clken;
    logic sigFromSrams_bore_108_ram_aux_clk;
    logic sigFromSrams_bore_108_ram_aux_ckbp;
    logic sigFromSrams_bore_108_ram_mcp_hold;
    logic sigFromSrams_bore_108_cgen;
    logic sigFromSrams_bore_109_ram_hold;
    logic sigFromSrams_bore_109_ram_bypass;
    logic sigFromSrams_bore_109_ram_bp_clken;
    logic sigFromSrams_bore_109_ram_aux_clk;
    logic sigFromSrams_bore_109_ram_aux_ckbp;
    logic sigFromSrams_bore_109_ram_mcp_hold;
    logic sigFromSrams_bore_109_cgen;
    logic sigFromSrams_bore_110_ram_hold;
    logic sigFromSrams_bore_110_ram_bypass;
    logic sigFromSrams_bore_110_ram_bp_clken;
    logic sigFromSrams_bore_110_ram_aux_clk;
    logic sigFromSrams_bore_110_ram_aux_ckbp;
    logic sigFromSrams_bore_110_ram_mcp_hold;
    logic sigFromSrams_bore_110_cgen;
    logic sigFromSrams_bore_111_ram_hold;
    logic sigFromSrams_bore_111_ram_bypass;
    logic sigFromSrams_bore_111_ram_bp_clken;
    logic sigFromSrams_bore_111_ram_aux_clk;
    logic sigFromSrams_bore_111_ram_aux_ckbp;
    logic sigFromSrams_bore_111_ram_mcp_hold;
    logic sigFromSrams_bore_111_cgen;
    logic sigFromSrams_bore_112_ram_hold;
    logic sigFromSrams_bore_112_ram_bypass;
    logic sigFromSrams_bore_112_ram_bp_clken;
    logic sigFromSrams_bore_112_ram_aux_clk;
    logic sigFromSrams_bore_112_ram_aux_ckbp;
    logic sigFromSrams_bore_112_ram_mcp_hold;
    logic sigFromSrams_bore_112_cgen;
    logic sigFromSrams_bore_113_ram_hold;
    logic sigFromSrams_bore_113_ram_bypass;
    logic sigFromSrams_bore_113_ram_bp_clken;
    logic sigFromSrams_bore_113_ram_aux_clk;
    logic sigFromSrams_bore_113_ram_aux_ckbp;
    logic sigFromSrams_bore_113_ram_mcp_hold;
    logic sigFromSrams_bore_113_cgen;
    logic sigFromSrams_bore_114_ram_hold;
    logic sigFromSrams_bore_114_ram_bypass;
    logic sigFromSrams_bore_114_ram_bp_clken;
    logic sigFromSrams_bore_114_ram_aux_clk;
    logic sigFromSrams_bore_114_ram_aux_ckbp;
    logic sigFromSrams_bore_114_ram_mcp_hold;
    logic sigFromSrams_bore_114_cgen;
    logic sigFromSrams_bore_115_ram_hold;
    logic sigFromSrams_bore_115_ram_bypass;
    logic sigFromSrams_bore_115_ram_bp_clken;
    logic sigFromSrams_bore_115_ram_aux_clk;
    logic sigFromSrams_bore_115_ram_aux_ckbp;
    logic sigFromSrams_bore_115_ram_mcp_hold;
    logic sigFromSrams_bore_115_cgen;
    logic sigFromSrams_bore_116_ram_hold;
    logic sigFromSrams_bore_116_ram_bypass;
    logic sigFromSrams_bore_116_ram_bp_clken;
    logic sigFromSrams_bore_116_ram_aux_clk;
    logic sigFromSrams_bore_116_ram_aux_ckbp;
    logic sigFromSrams_bore_116_ram_mcp_hold;
    logic sigFromSrams_bore_116_cgen;
    logic sigFromSrams_bore_117_ram_hold;
    logic sigFromSrams_bore_117_ram_bypass;
    logic sigFromSrams_bore_117_ram_bp_clken;
    logic sigFromSrams_bore_117_ram_aux_clk;
    logic sigFromSrams_bore_117_ram_aux_ckbp;
    logic sigFromSrams_bore_117_ram_mcp_hold;
    logic sigFromSrams_bore_117_cgen;
    logic sigFromSrams_bore_118_ram_hold;
    logic sigFromSrams_bore_118_ram_bypass;
    logic sigFromSrams_bore_118_ram_bp_clken;
    logic sigFromSrams_bore_118_ram_aux_clk;
    logic sigFromSrams_bore_118_ram_aux_ckbp;
    logic sigFromSrams_bore_118_ram_mcp_hold;
    logic sigFromSrams_bore_118_cgen;
    logic sigFromSrams_bore_119_ram_hold;
    logic sigFromSrams_bore_119_ram_bypass;
    logic sigFromSrams_bore_119_ram_bp_clken;
    logic sigFromSrams_bore_119_ram_aux_clk;
    logic sigFromSrams_bore_119_ram_aux_ckbp;
    logic sigFromSrams_bore_119_ram_mcp_hold;
    logic sigFromSrams_bore_119_cgen;
    logic sigFromSrams_bore_120_ram_hold;
    logic sigFromSrams_bore_120_ram_bypass;
    logic sigFromSrams_bore_120_ram_bp_clken;
    logic sigFromSrams_bore_120_ram_aux_clk;
    logic sigFromSrams_bore_120_ram_aux_ckbp;
    logic sigFromSrams_bore_120_ram_mcp_hold;
    logic sigFromSrams_bore_120_cgen;
    logic sigFromSrams_bore_121_ram_hold;
    logic sigFromSrams_bore_121_ram_bypass;
    logic sigFromSrams_bore_121_ram_bp_clken;
    logic sigFromSrams_bore_121_ram_aux_clk;
    logic sigFromSrams_bore_121_ram_aux_ckbp;
    logic sigFromSrams_bore_121_ram_mcp_hold;
    logic sigFromSrams_bore_121_cgen;
    logic sigFromSrams_bore_122_ram_hold;
    logic sigFromSrams_bore_122_ram_bypass;
    logic sigFromSrams_bore_122_ram_bp_clken;
    logic sigFromSrams_bore_122_ram_aux_clk;
    logic sigFromSrams_bore_122_ram_aux_ckbp;
    logic sigFromSrams_bore_122_ram_mcp_hold;
    logic sigFromSrams_bore_122_cgen;
    logic sigFromSrams_bore_123_ram_hold;
    logic sigFromSrams_bore_123_ram_bypass;
    logic sigFromSrams_bore_123_ram_bp_clken;
    logic sigFromSrams_bore_123_ram_aux_clk;
    logic sigFromSrams_bore_123_ram_aux_ckbp;
    logic sigFromSrams_bore_123_ram_mcp_hold;
    logic sigFromSrams_bore_123_cgen;
    logic sigFromSrams_bore_124_ram_hold;
    logic sigFromSrams_bore_124_ram_bypass;
    logic sigFromSrams_bore_124_ram_bp_clken;
    logic sigFromSrams_bore_124_ram_aux_clk;
    logic sigFromSrams_bore_124_ram_aux_ckbp;
    logic sigFromSrams_bore_124_ram_mcp_hold;
    logic sigFromSrams_bore_124_cgen;
    logic sigFromSrams_bore_125_ram_hold;
    logic sigFromSrams_bore_125_ram_bypass;
    logic sigFromSrams_bore_125_ram_bp_clken;
    logic sigFromSrams_bore_125_ram_aux_clk;
    logic sigFromSrams_bore_125_ram_aux_ckbp;
    logic sigFromSrams_bore_125_ram_mcp_hold;
    logic sigFromSrams_bore_125_cgen;
    logic sigFromSrams_bore_126_ram_hold;
    logic sigFromSrams_bore_126_ram_bypass;
    logic sigFromSrams_bore_126_ram_bp_clken;
    logic sigFromSrams_bore_126_ram_aux_clk;
    logic sigFromSrams_bore_126_ram_aux_ckbp;
    logic sigFromSrams_bore_126_ram_mcp_hold;
    logic sigFromSrams_bore_126_cgen;
    logic sigFromSrams_bore_127_ram_hold;
    logic sigFromSrams_bore_127_ram_bypass;
    logic sigFromSrams_bore_127_ram_bp_clken;
    logic sigFromSrams_bore_127_ram_aux_clk;
    logic sigFromSrams_bore_127_ram_aux_ckbp;
    logic sigFromSrams_bore_127_ram_mcp_hold;
    logic sigFromSrams_bore_127_cgen;
    logic sigFromSrams_bore_128_ram_hold;
    logic sigFromSrams_bore_128_ram_bypass;
    logic sigFromSrams_bore_128_ram_bp_clken;
    logic sigFromSrams_bore_128_ram_aux_clk;
    logic sigFromSrams_bore_128_ram_aux_ckbp;
    logic sigFromSrams_bore_128_ram_mcp_hold;
    logic sigFromSrams_bore_128_cgen;
    logic sigFromSrams_bore_129_ram_hold;
    logic sigFromSrams_bore_129_ram_bypass;
    logic sigFromSrams_bore_129_ram_bp_clken;
    logic sigFromSrams_bore_129_ram_aux_clk;
    logic sigFromSrams_bore_129_ram_aux_ckbp;
    logic sigFromSrams_bore_129_ram_mcp_hold;
    logic sigFromSrams_bore_129_cgen;
    logic sigFromSrams_bore_130_ram_hold;
    logic sigFromSrams_bore_130_ram_bypass;
    logic sigFromSrams_bore_130_ram_bp_clken;
    logic sigFromSrams_bore_130_ram_aux_clk;
    logic sigFromSrams_bore_130_ram_aux_ckbp;
    logic sigFromSrams_bore_130_ram_mcp_hold;
    logic sigFromSrams_bore_130_cgen;
    logic sigFromSrams_bore_131_ram_hold;
    logic sigFromSrams_bore_131_ram_bypass;
    logic sigFromSrams_bore_131_ram_bp_clken;
    logic sigFromSrams_bore_131_ram_aux_clk;
    logic sigFromSrams_bore_131_ram_aux_ckbp;
    logic sigFromSrams_bore_131_ram_mcp_hold;
    logic sigFromSrams_bore_131_cgen;
    logic sigFromSrams_bore_132_ram_hold;
    logic sigFromSrams_bore_132_ram_bypass;
    logic sigFromSrams_bore_132_ram_bp_clken;
    logic sigFromSrams_bore_132_ram_aux_clk;
    logic sigFromSrams_bore_132_ram_aux_ckbp;
    logic sigFromSrams_bore_132_ram_mcp_hold;
    logic sigFromSrams_bore_132_cgen;
    logic sigFromSrams_bore_133_ram_hold;
    logic sigFromSrams_bore_133_ram_bypass;
    logic sigFromSrams_bore_133_ram_bp_clken;
    logic sigFromSrams_bore_133_ram_aux_clk;
    logic sigFromSrams_bore_133_ram_aux_ckbp;
    logic sigFromSrams_bore_133_ram_mcp_hold;
    logic sigFromSrams_bore_133_cgen;
    logic sigFromSrams_bore_134_ram_hold;
    logic sigFromSrams_bore_134_ram_bypass;
    logic sigFromSrams_bore_134_ram_bp_clken;
    logic sigFromSrams_bore_134_ram_aux_clk;
    logic sigFromSrams_bore_134_ram_aux_ckbp;
    logic sigFromSrams_bore_134_ram_mcp_hold;
    logic sigFromSrams_bore_134_cgen;
    logic sigFromSrams_bore_135_ram_hold;
    logic sigFromSrams_bore_135_ram_bypass;
    logic sigFromSrams_bore_135_ram_bp_clken;
    logic sigFromSrams_bore_135_ram_aux_clk;
    logic sigFromSrams_bore_135_ram_aux_ckbp;
    logic sigFromSrams_bore_135_ram_mcp_hold;
    logic sigFromSrams_bore_135_cgen;
    logic sigFromSrams_bore_136_ram_hold;
    logic sigFromSrams_bore_136_ram_bypass;
    logic sigFromSrams_bore_136_ram_bp_clken;
    logic sigFromSrams_bore_136_ram_aux_clk;
    logic sigFromSrams_bore_136_ram_aux_ckbp;
    logic sigFromSrams_bore_136_ram_mcp_hold;
    logic sigFromSrams_bore_136_cgen;
    logic sigFromSrams_bore_137_ram_hold;
    logic sigFromSrams_bore_137_ram_bypass;
    logic sigFromSrams_bore_137_ram_bp_clken;
    logic sigFromSrams_bore_137_ram_aux_clk;
    logic sigFromSrams_bore_137_ram_aux_ckbp;
    logic sigFromSrams_bore_137_ram_mcp_hold;
    logic sigFromSrams_bore_137_cgen;
    logic sigFromSrams_bore_138_ram_hold;
    logic sigFromSrams_bore_138_ram_bypass;
    logic sigFromSrams_bore_138_ram_bp_clken;
    logic sigFromSrams_bore_138_ram_aux_clk;
    logic sigFromSrams_bore_138_ram_aux_ckbp;
    logic sigFromSrams_bore_138_ram_mcp_hold;
    logic sigFromSrams_bore_138_cgen;
    logic sigFromSrams_bore_139_ram_hold;
    logic sigFromSrams_bore_139_ram_bypass;
    logic sigFromSrams_bore_139_ram_bp_clken;
    logic sigFromSrams_bore_139_ram_aux_clk;
    logic sigFromSrams_bore_139_ram_aux_ckbp;
    logic sigFromSrams_bore_139_ram_mcp_hold;
    logic sigFromSrams_bore_139_cgen;
    logic sigFromSrams_bore_140_ram_hold;
    logic sigFromSrams_bore_140_ram_bypass;
    logic sigFromSrams_bore_140_ram_bp_clken;
    logic sigFromSrams_bore_140_ram_aux_clk;
    logic sigFromSrams_bore_140_ram_aux_ckbp;
    logic sigFromSrams_bore_140_ram_mcp_hold;
    logic sigFromSrams_bore_140_cgen;
    logic sigFromSrams_bore_141_ram_hold;
    logic sigFromSrams_bore_141_ram_bypass;
    logic sigFromSrams_bore_141_ram_bp_clken;
    logic sigFromSrams_bore_141_ram_aux_clk;
    logic sigFromSrams_bore_141_ram_aux_ckbp;
    logic sigFromSrams_bore_141_ram_mcp_hold;
    logic sigFromSrams_bore_141_cgen;
    logic sigFromSrams_bore_142_ram_hold;
    logic sigFromSrams_bore_142_ram_bypass;
    logic sigFromSrams_bore_142_ram_bp_clken;
    logic sigFromSrams_bore_142_ram_aux_clk;
    logic sigFromSrams_bore_142_ram_aux_ckbp;
    logic sigFromSrams_bore_142_ram_mcp_hold;
    logic sigFromSrams_bore_142_cgen;
    logic sigFromSrams_bore_143_ram_hold;
    logic sigFromSrams_bore_143_ram_bypass;
    logic sigFromSrams_bore_143_ram_bp_clken;
    logic sigFromSrams_bore_143_ram_aux_clk;
    logic sigFromSrams_bore_143_ram_aux_ckbp;
    logic sigFromSrams_bore_143_ram_mcp_hold;
    logic sigFromSrams_bore_143_cgen;

    assign io_ctrl_ubtbEnable = 1'b1;
    assign io_ctrl_abtbEnable = 1'b1;
    assign io_ctrl_mbtbEnable = 1'b1;
    assign io_ctrl_tageEnable = 1'b1;
    assign io_ctrl_scEnable = 1'b1;
    assign io_ctrl_ittageEnable = 1'b1;
    assign io_resetVector_addr = stim_wide[46:0];
    assign io_fromFtq_redirect_valid = stim_wide[0];
    assign io_fromFtq_redirect_bits_cfiPc_addr = stim_wide[48:0];
    assign io_fromFtq_redirect_bits_target_addr = stim_wide[48:0];
    assign io_fromFtq_redirect_bits_taken = stim_wide[0];
    assign io_fromFtq_redirect_bits_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_meta_phr_phrPtr_flag = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_phr_phrPtr_value = stim_wide[9:0];
    assign io_fromFtq_redirect_bits_meta_phr_phrLowBits = stim_wide[12:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_ghr = stim_wide[15:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_bw = stim_wide[7:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_0 = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_1 = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_2 = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_3 = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_4 = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_5 = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_6 = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_7 = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_0_branchType = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_1_branchType = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_2_branchType = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_3_branchType = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_4_branchType = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_5_branchType = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_6_branchType = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_7_branchType = stim_wide[1:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_position_0 = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_position_1 = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_position_2 = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_position_3 = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_position_4 = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_position_5 = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_position_6 = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_commonHRMeta_position_7 = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_ras_ssp = stim_wide[3:0];
    assign io_fromFtq_redirect_bits_meta_ras_sctr = stim_wide[2:0];
    assign io_fromFtq_redirect_bits_meta_ras_tosw_flag = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_ras_tosw_value = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_ras_tosr_flag = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_ras_tosr_value = stim_wide[4:0];
    assign io_fromFtq_redirect_bits_meta_ras_nos_flag = stim_wide[0];
    assign io_fromFtq_redirect_bits_meta_ras_nos_value = stim_wide[4:0];
    assign io_fromFtq_train_valid = stim_wide[0];
    assign io_fromFtq_train_bits_startPc_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_branches_0_valid = stim_wide[0];
    assign io_fromFtq_train_bits_branches_0_bits_target_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_branches_0_bits_taken = stim_wide[0];
    assign io_fromFtq_train_bits_branches_0_bits_cfiPosition = stim_wide[4:0];
    assign io_fromFtq_train_bits_branches_0_bits_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_0_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_0_bits_mispredict = stim_wide[0];
    assign io_fromFtq_train_bits_branches_1_valid = stim_wide[0];
    assign io_fromFtq_train_bits_branches_1_bits_target_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_branches_1_bits_taken = stim_wide[0];
    assign io_fromFtq_train_bits_branches_1_bits_cfiPosition = stim_wide[4:0];
    assign io_fromFtq_train_bits_branches_1_bits_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_1_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_1_bits_mispredict = stim_wide[0];
    assign io_fromFtq_train_bits_branches_2_valid = stim_wide[0];
    assign io_fromFtq_train_bits_branches_2_bits_target_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_branches_2_bits_taken = stim_wide[0];
    assign io_fromFtq_train_bits_branches_2_bits_cfiPosition = stim_wide[4:0];
    assign io_fromFtq_train_bits_branches_2_bits_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_2_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_2_bits_mispredict = stim_wide[0];
    assign io_fromFtq_train_bits_branches_3_valid = stim_wide[0];
    assign io_fromFtq_train_bits_branches_3_bits_target_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_branches_3_bits_taken = stim_wide[0];
    assign io_fromFtq_train_bits_branches_3_bits_cfiPosition = stim_wide[4:0];
    assign io_fromFtq_train_bits_branches_3_bits_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_3_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_3_bits_mispredict = stim_wide[0];
    assign io_fromFtq_train_bits_branches_4_valid = stim_wide[0];
    assign io_fromFtq_train_bits_branches_4_bits_target_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_branches_4_bits_taken = stim_wide[0];
    assign io_fromFtq_train_bits_branches_4_bits_cfiPosition = stim_wide[4:0];
    assign io_fromFtq_train_bits_branches_4_bits_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_4_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_4_bits_mispredict = stim_wide[0];
    assign io_fromFtq_train_bits_branches_5_valid = stim_wide[0];
    assign io_fromFtq_train_bits_branches_5_bits_target_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_branches_5_bits_taken = stim_wide[0];
    assign io_fromFtq_train_bits_branches_5_bits_cfiPosition = stim_wide[4:0];
    assign io_fromFtq_train_bits_branches_5_bits_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_5_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_5_bits_mispredict = stim_wide[0];
    assign io_fromFtq_train_bits_branches_6_valid = stim_wide[0];
    assign io_fromFtq_train_bits_branches_6_bits_target_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_branches_6_bits_taken = stim_wide[0];
    assign io_fromFtq_train_bits_branches_6_bits_cfiPosition = stim_wide[4:0];
    assign io_fromFtq_train_bits_branches_6_bits_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_6_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_6_bits_mispredict = stim_wide[0];
    assign io_fromFtq_train_bits_branches_7_valid = stim_wide[0];
    assign io_fromFtq_train_bits_branches_7_bits_target_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_branches_7_bits_taken = stim_wide[0];
    assign io_fromFtq_train_bits_branches_7_bits_cfiPosition = stim_wide[4:0];
    assign io_fromFtq_train_bits_branches_7_bits_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_7_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_branches_7_bits_mispredict = stim_wide[0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_0_rawHit = stim_wide[0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_0_position = stim_wide[4:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_0_counter_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_1_rawHit = stim_wide[0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_1_position = stim_wide[4:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_1_counter_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_2_rawHit = stim_wide[0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_2_position = stim_wide[4:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_2_counter_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_3_rawHit = stim_wide[0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_3_position = stim_wide[4:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_0_3_counter_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_0_rawHit = stim_wide[0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_0_position = stim_wide[4:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_0_counter_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_1_rawHit = stim_wide[0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_1_position = stim_wide[4:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_1_counter_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_2_rawHit = stim_wide[0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_2_position = stim_wide[4:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_2_counter_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_3_rawHit = stim_wide[0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_3_position = stim_wide[4:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_branchType = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_mbtb_entries_1_3_counter_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_0_useProvider = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_0_providerTableIdx = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_0_providerWayIdx = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_0_providerTakenCtr_value = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_0_providerUsefulCtr_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_0_altOrBasePred = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_1_useProvider = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_1_providerTableIdx = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_1_providerWayIdx = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_1_providerTakenCtr_value = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_1_providerUsefulCtr_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_1_altOrBasePred = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_2_useProvider = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_2_providerTableIdx = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_2_providerWayIdx = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_2_providerTakenCtr_value = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_2_providerUsefulCtr_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_2_altOrBasePred = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_3_useProvider = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_3_providerTableIdx = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_3_providerWayIdx = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_3_providerTakenCtr_value = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_3_providerUsefulCtr_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_3_altOrBasePred = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_4_useProvider = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_4_providerTableIdx = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_4_providerWayIdx = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_4_providerTakenCtr_value = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_4_providerUsefulCtr_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_4_altOrBasePred = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_5_useProvider = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_5_providerTableIdx = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_5_providerWayIdx = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_5_providerTakenCtr_value = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_5_providerUsefulCtr_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_5_altOrBasePred = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_6_useProvider = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_6_providerTableIdx = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_6_providerWayIdx = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_6_providerTakenCtr_value = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_6_providerUsefulCtr_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_6_altOrBasePred = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_7_useProvider = stim_wide[0];
    assign io_fromFtq_train_bits_meta_tage_entries_7_providerTableIdx = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_7_providerWayIdx = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_7_providerTakenCtr_value = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_tage_entries_7_providerUsefulCtr_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_tage_entries_7_altOrBasePred = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_0_0 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_0_1 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_0_2 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_0_3 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_0_4 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_0_5 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_0_6 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_0_7 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_1_0 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_1_1 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_1_2 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_1_3 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_1_4 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_1_5 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_1_6 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scPathResp_1_7 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_0 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_1 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_2 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_3 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_4 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_5 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_6 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_7 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_8 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_9 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_10 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_11 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_12 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_13 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_14 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_15 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_16 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_17 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_18 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_19 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_20 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_21 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_22 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_23 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_24 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_25 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_26 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_27 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_28 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_29 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_30 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasResp_31 = stim_wide[5:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasLowerBits_0 = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasLowerBits_1 = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasLowerBits_2 = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasLowerBits_3 = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasLowerBits_4 = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasLowerBits_5 = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasLowerBits_6 = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_sc_scBiasLowerBits_7 = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_sc_scCommonHR_valid = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_scCommonHR_ghr = stim_wide[15:0];
    assign io_fromFtq_train_bits_meta_sc_scCommonHR_bw = stim_wide[7:0];
    assign io_fromFtq_train_bits_meta_sc_scPred_0 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_scPred_1 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_scPred_2 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_scPred_3 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_scPred_4 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_scPred_5 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_scPred_6 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_scPred_7 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePred_0 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePred_1 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePred_2 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePred_3 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePred_4 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePred_5 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePred_6 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePred_7 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePredValid_0 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePredValid_1 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePredValid_2 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePredValid_3 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePredValid_4 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePredValid_5 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePredValid_6 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_tagePredValid_7 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_useScPred_0 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_useScPred_1 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_useScPred_2 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_useScPred_3 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_useScPred_4 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_useScPred_5 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_useScPred_6 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_useScPred_7 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_sumAboveThres_0 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_sumAboveThres_1 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_sumAboveThres_2 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_sumAboveThres_3 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_sumAboveThres_4 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_sumAboveThres_5 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_sumAboveThres_6 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_sumAboveThres_7 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_0 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_1 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_2 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_3 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_4 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_5 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_6 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_7 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_0 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_1 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_2 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_3 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_4 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_5 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_6 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_7 = stim_wide[0];
    assign io_fromFtq_train_bits_meta_sc_debug_predPathIdx_0 = stim_wide[6:0];
    assign io_fromFtq_train_bits_meta_sc_debug_predPathIdx_1 = stim_wide[6:0];
    assign io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_0 = stim_wide[6:0];
    assign io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_1 = stim_wide[6:0];
    assign io_fromFtq_train_bits_meta_sc_debug_predBWIdx_0 = stim_wide[6:0];
    assign io_fromFtq_train_bits_meta_sc_debug_predBWIdx_1 = stim_wide[6:0];
    assign io_fromFtq_train_bits_meta_sc_debug_predBiasIdx = stim_wide[6:0];
    assign io_fromFtq_train_bits_meta_ittage_provider_valid = stim_wide[0];
    assign io_fromFtq_train_bits_meta_ittage_provider_bits = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_ittage_altProvider_valid = stim_wide[0];
    assign io_fromFtq_train_bits_meta_ittage_altProvider_bits = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_ittage_altDiffers = stim_wide[0];
    assign io_fromFtq_train_bits_meta_ittage_providerUsefulCnt_value = stim_wide[0];
    assign io_fromFtq_train_bits_meta_ittage_providerCnt_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_ittage_altProviderCnt_value = stim_wide[1:0];
    assign io_fromFtq_train_bits_meta_ittage_allocate_valid = stim_wide[0];
    assign io_fromFtq_train_bits_meta_ittage_allocate_bits = stim_wide[2:0];
    assign io_fromFtq_train_bits_meta_ittage_providerTarget_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_meta_ittage_altProviderTarget_addr = stim_wide[48:0];
    assign io_fromFtq_train_bits_meta_phr_phrPtr_value = stim_wide[9:0];
    assign io_fromFtq_train_bits_meta_phr_phrLowBits = stim_wide[12:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist = stim_wide[12:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist = stim_wide[11:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist = stim_wide[12:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist = stim_wide[11:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist = stim_wide[12:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist = stim_wide[11:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist = stim_wide[12:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist = stim_wide[11:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist = stim_wide[7:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist = stim_wide[12:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist = stim_wide[11:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist = stim_wide[12:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist = stim_wide[11:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist = stim_wide[11:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist = stim_wide[10:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist = stim_wide[7:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist = stim_wide[6:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist = stim_wide[7:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist = stim_wide[8:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist = stim_wide[7:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist = stim_wide[7:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist = stim_wide[6:0];
    assign io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist = stim_wide[3:0];
    assign io_fromFtq_commit_valid = stim_wide[0];
    assign io_fromFtq_commit_bits_meta_ras_ssp = stim_wide[3:0];
    assign io_fromFtq_commit_bits_meta_ras_tosw_flag = stim_wide[0];
    assign io_fromFtq_commit_bits_meta_ras_tosw_value = stim_wide[4:0];
    assign io_fromFtq_commit_bits_attribute_rasAction = stim_wide[1:0];
    assign io_fromFtq_bpuPtr_flag = stim_wide[0];
    assign io_fromFtq_bpuPtr_value = stim_wide[5:0];
    assign boreChildrenBd_bore_array = stim_wide[7:0];
    assign boreChildrenBd_bore_all = stim_wide[0];
    assign boreChildrenBd_bore_req = stim_wide[0];
    assign boreChildrenBd_bore_writeen = stim_wide[0];
    assign boreChildrenBd_bore_be = stim_wide[37:0];
    assign boreChildrenBd_bore_addr = stim_wide[9:0];
    assign boreChildrenBd_bore_indata = stim_wide[111:0];
    assign boreChildrenBd_bore_readen = stim_wide[0];
    assign boreChildrenBd_bore_addr_rd = stim_wide[9:0];
    assign boreChildrenBd_bore_1_array = stim_wide[7:0];
    assign boreChildrenBd_bore_1_all = stim_wide[0];
    assign boreChildrenBd_bore_1_req = stim_wide[0];
    assign boreChildrenBd_bore_1_writeen = stim_wide[0];
    assign boreChildrenBd_bore_1_be = stim_wide[37:0];
    assign boreChildrenBd_bore_1_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_1_indata = stim_wide[37:0];
    assign boreChildrenBd_bore_1_readen = stim_wide[0];
    assign boreChildrenBd_bore_1_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_2_array = stim_wide[7:0];
    assign boreChildrenBd_bore_2_all = stim_wide[0];
    assign boreChildrenBd_bore_2_req = stim_wide[0];
    assign boreChildrenBd_bore_2_writeen = stim_wide[0];
    assign boreChildrenBd_bore_2_be = stim_wide[75:0];
    assign boreChildrenBd_bore_2_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_2_indata = stim_wide[75:0];
    assign boreChildrenBd_bore_2_readen = stim_wide[0];
    assign boreChildrenBd_bore_2_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_3_array = stim_wide[7:0];
    assign boreChildrenBd_bore_3_all = stim_wide[0];
    assign boreChildrenBd_bore_3_req = stim_wide[0];
    assign boreChildrenBd_bore_3_writeen = stim_wide[0];
    assign boreChildrenBd_bore_3_be = stim_wide[75:0];
    assign boreChildrenBd_bore_3_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_3_indata = stim_wide[75:0];
    assign boreChildrenBd_bore_3_readen = stim_wide[0];
    assign boreChildrenBd_bore_3_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_4_array = stim_wide[7:0];
    assign boreChildrenBd_bore_4_all = stim_wide[0];
    assign boreChildrenBd_bore_4_req = stim_wide[0];
    assign boreChildrenBd_bore_4_writeen = stim_wide[0];
    assign boreChildrenBd_bore_4_be = stim_wide[75:0];
    assign boreChildrenBd_bore_4_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_4_indata = stim_wide[75:0];
    assign boreChildrenBd_bore_4_readen = stim_wide[0];
    assign boreChildrenBd_bore_4_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_5_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_5_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_5_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_5_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_5_re = stim_wide[0];
    assign boreChildrenBd_bore_5_we = stim_wide[0];
    assign boreChildrenBd_bore_5_ack = stim_wide[0];
    assign boreChildrenBd_bore_5_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_5_array = stim_wide[7:0];
    assign boreChildrenBd_bore_6_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_6_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_6_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_6_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_6_re = stim_wide[0];
    assign boreChildrenBd_bore_6_we = stim_wide[0];
    assign boreChildrenBd_bore_6_ack = stim_wide[0];
    assign boreChildrenBd_bore_6_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_6_array = stim_wide[7:0];
    assign boreChildrenBd_bore_7_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_7_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_7_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_7_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_7_re = stim_wide[0];
    assign boreChildrenBd_bore_7_we = stim_wide[0];
    assign boreChildrenBd_bore_7_ack = stim_wide[0];
    assign boreChildrenBd_bore_7_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_7_array = stim_wide[7:0];
    assign boreChildrenBd_bore_8_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_8_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_8_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_8_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_8_re = stim_wide[0];
    assign boreChildrenBd_bore_8_we = stim_wide[0];
    assign boreChildrenBd_bore_8_ack = stim_wide[0];
    assign boreChildrenBd_bore_8_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_8_array = stim_wide[7:0];
    assign boreChildrenBd_bore_9_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_9_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_9_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_9_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_9_re = stim_wide[0];
    assign boreChildrenBd_bore_9_we = stim_wide[0];
    assign boreChildrenBd_bore_9_ack = stim_wide[0];
    assign boreChildrenBd_bore_9_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_9_array = stim_wide[7:0];
    assign boreChildrenBd_bore_10_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_10_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_10_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_10_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_10_re = stim_wide[0];
    assign boreChildrenBd_bore_10_we = stim_wide[0];
    assign boreChildrenBd_bore_10_ack = stim_wide[0];
    assign boreChildrenBd_bore_10_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_10_array = stim_wide[7:0];
    assign boreChildrenBd_bore_11_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_11_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_11_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_11_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_11_re = stim_wide[0];
    assign boreChildrenBd_bore_11_we = stim_wide[0];
    assign boreChildrenBd_bore_11_ack = stim_wide[0];
    assign boreChildrenBd_bore_11_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_11_array = stim_wide[7:0];
    assign boreChildrenBd_bore_12_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_12_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_12_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_12_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_12_re = stim_wide[0];
    assign boreChildrenBd_bore_12_we = stim_wide[0];
    assign boreChildrenBd_bore_12_ack = stim_wide[0];
    assign boreChildrenBd_bore_12_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_12_array = stim_wide[7:0];
    assign boreChildrenBd_bore_13_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_13_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_13_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_13_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_13_re = stim_wide[0];
    assign boreChildrenBd_bore_13_we = stim_wide[0];
    assign boreChildrenBd_bore_13_ack = stim_wide[0];
    assign boreChildrenBd_bore_13_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_13_array = stim_wide[7:0];
    assign boreChildrenBd_bore_14_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_14_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_14_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_14_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_14_re = stim_wide[0];
    assign boreChildrenBd_bore_14_we = stim_wide[0];
    assign boreChildrenBd_bore_14_ack = stim_wide[0];
    assign boreChildrenBd_bore_14_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_14_array = stim_wide[7:0];
    assign boreChildrenBd_bore_15_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_15_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_15_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_15_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_15_re = stim_wide[0];
    assign boreChildrenBd_bore_15_we = stim_wide[0];
    assign boreChildrenBd_bore_15_ack = stim_wide[0];
    assign boreChildrenBd_bore_15_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_15_array = stim_wide[7:0];
    assign boreChildrenBd_bore_16_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_16_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_16_wdata = stim_wide[47:0];
    assign boreChildrenBd_bore_16_wmask = stim_wide[7:0];
    assign boreChildrenBd_bore_16_re = stim_wide[0];
    assign boreChildrenBd_bore_16_we = stim_wide[0];
    assign boreChildrenBd_bore_16_ack = stim_wide[0];
    assign boreChildrenBd_bore_16_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_16_array = stim_wide[7:0];
    assign boreChildrenBd_bore_17_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_17_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_17_wdata = stim_wide[191:0];
    assign boreChildrenBd_bore_17_wmask = stim_wide[31:0];
    assign boreChildrenBd_bore_17_re = stim_wide[0];
    assign boreChildrenBd_bore_17_we = stim_wide[0];
    assign boreChildrenBd_bore_17_ack = stim_wide[0];
    assign boreChildrenBd_bore_17_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_17_array = stim_wide[7:0];
    assign boreChildrenBd_bore_18_addr = stim_wide[7:0];
    assign boreChildrenBd_bore_18_addr_rd = stim_wide[7:0];
    assign boreChildrenBd_bore_18_wdata = stim_wide[191:0];
    assign boreChildrenBd_bore_18_wmask = stim_wide[31:0];
    assign boreChildrenBd_bore_18_re = stim_wide[0];
    assign boreChildrenBd_bore_18_we = stim_wide[0];
    assign boreChildrenBd_bore_18_ack = stim_wide[0];
    assign boreChildrenBd_bore_18_selectedOH = stim_wide[0];
    assign boreChildrenBd_bore_18_array = stim_wide[7:0];
    assign sigFromSrams_bore_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_cgen = stim_wide[0];
    assign sigFromSrams_bore_1_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_1_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_1_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_1_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_1_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_1_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_1_cgen = stim_wide[0];
    assign sigFromSrams_bore_2_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_2_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_2_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_2_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_2_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_2_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_2_cgen = stim_wide[0];
    assign sigFromSrams_bore_3_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_3_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_3_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_3_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_3_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_3_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_3_cgen = stim_wide[0];
    assign sigFromSrams_bore_4_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_4_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_4_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_4_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_4_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_4_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_4_cgen = stim_wide[0];
    assign sigFromSrams_bore_5_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_5_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_5_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_5_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_5_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_5_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_5_cgen = stim_wide[0];
    assign sigFromSrams_bore_6_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_6_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_6_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_6_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_6_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_6_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_6_cgen = stim_wide[0];
    assign sigFromSrams_bore_7_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_7_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_7_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_7_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_7_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_7_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_7_cgen = stim_wide[0];
    assign sigFromSrams_bore_8_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_8_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_8_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_8_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_8_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_8_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_8_cgen = stim_wide[0];
    assign sigFromSrams_bore_9_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_9_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_9_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_9_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_9_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_9_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_9_cgen = stim_wide[0];
    assign sigFromSrams_bore_10_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_10_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_10_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_10_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_10_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_10_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_10_cgen = stim_wide[0];
    assign sigFromSrams_bore_11_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_11_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_11_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_11_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_11_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_11_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_11_cgen = stim_wide[0];
    assign sigFromSrams_bore_12_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_12_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_12_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_12_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_12_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_12_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_12_cgen = stim_wide[0];
    assign sigFromSrams_bore_13_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_13_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_13_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_13_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_13_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_13_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_13_cgen = stim_wide[0];
    assign sigFromSrams_bore_14_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_14_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_14_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_14_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_14_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_14_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_14_cgen = stim_wide[0];
    assign sigFromSrams_bore_15_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_15_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_15_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_15_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_15_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_15_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_15_cgen = stim_wide[0];
    assign sigFromSrams_bore_16_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_16_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_16_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_16_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_16_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_16_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_16_cgen = stim_wide[0];
    assign sigFromSrams_bore_17_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_17_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_17_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_17_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_17_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_17_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_17_cgen = stim_wide[0];
    assign sigFromSrams_bore_18_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_18_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_18_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_18_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_18_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_18_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_18_cgen = stim_wide[0];
    assign sigFromSrams_bore_19_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_19_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_19_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_19_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_19_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_19_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_19_cgen = stim_wide[0];
    assign sigFromSrams_bore_20_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_20_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_20_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_20_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_20_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_20_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_20_cgen = stim_wide[0];
    assign sigFromSrams_bore_21_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_21_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_21_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_21_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_21_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_21_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_21_cgen = stim_wide[0];
    assign sigFromSrams_bore_22_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_22_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_22_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_22_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_22_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_22_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_22_cgen = stim_wide[0];
    assign sigFromSrams_bore_23_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_23_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_23_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_23_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_23_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_23_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_23_cgen = stim_wide[0];
    assign sigFromSrams_bore_24_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_24_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_24_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_24_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_24_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_24_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_24_cgen = stim_wide[0];
    assign sigFromSrams_bore_25_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_25_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_25_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_25_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_25_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_25_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_25_cgen = stim_wide[0];
    assign sigFromSrams_bore_26_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_26_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_26_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_26_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_26_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_26_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_26_cgen = stim_wide[0];
    assign sigFromSrams_bore_27_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_27_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_27_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_27_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_27_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_27_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_27_cgen = stim_wide[0];
    assign sigFromSrams_bore_28_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_28_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_28_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_28_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_28_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_28_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_28_cgen = stim_wide[0];
    assign sigFromSrams_bore_29_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_29_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_29_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_29_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_29_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_29_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_29_cgen = stim_wide[0];
    assign sigFromSrams_bore_30_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_30_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_30_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_30_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_30_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_30_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_30_cgen = stim_wide[0];
    assign sigFromSrams_bore_31_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_31_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_31_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_31_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_31_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_31_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_31_cgen = stim_wide[0];
    assign sigFromSrams_bore_32_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_32_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_32_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_32_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_32_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_32_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_32_cgen = stim_wide[0];
    assign sigFromSrams_bore_33_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_33_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_33_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_33_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_33_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_33_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_33_cgen = stim_wide[0];
    assign sigFromSrams_bore_34_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_34_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_34_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_34_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_34_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_34_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_34_cgen = stim_wide[0];
    assign sigFromSrams_bore_35_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_35_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_35_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_35_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_35_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_35_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_35_cgen = stim_wide[0];
    assign sigFromSrams_bore_36_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_36_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_36_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_36_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_36_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_36_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_36_cgen = stim_wide[0];
    assign sigFromSrams_bore_37_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_37_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_37_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_37_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_37_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_37_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_37_cgen = stim_wide[0];
    assign sigFromSrams_bore_38_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_38_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_38_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_38_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_38_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_38_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_38_cgen = stim_wide[0];
    assign sigFromSrams_bore_39_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_39_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_39_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_39_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_39_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_39_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_39_cgen = stim_wide[0];
    assign sigFromSrams_bore_40_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_40_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_40_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_40_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_40_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_40_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_40_cgen = stim_wide[0];
    assign sigFromSrams_bore_41_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_41_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_41_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_41_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_41_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_41_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_41_cgen = stim_wide[0];
    assign sigFromSrams_bore_42_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_42_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_42_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_42_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_42_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_42_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_42_cgen = stim_wide[0];
    assign sigFromSrams_bore_43_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_43_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_43_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_43_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_43_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_43_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_43_cgen = stim_wide[0];
    assign sigFromSrams_bore_44_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_44_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_44_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_44_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_44_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_44_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_44_cgen = stim_wide[0];
    assign sigFromSrams_bore_45_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_45_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_45_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_45_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_45_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_45_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_45_cgen = stim_wide[0];
    assign sigFromSrams_bore_46_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_46_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_46_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_46_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_46_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_46_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_46_cgen = stim_wide[0];
    assign sigFromSrams_bore_47_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_47_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_47_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_47_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_47_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_47_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_47_cgen = stim_wide[0];
    assign sigFromSrams_bore_48_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_48_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_48_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_48_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_48_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_48_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_48_cgen = stim_wide[0];
    assign sigFromSrams_bore_49_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_49_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_49_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_49_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_49_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_49_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_49_cgen = stim_wide[0];
    assign sigFromSrams_bore_50_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_50_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_50_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_50_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_50_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_50_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_50_cgen = stim_wide[0];
    assign sigFromSrams_bore_51_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_51_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_51_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_51_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_51_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_51_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_51_cgen = stim_wide[0];
    assign sigFromSrams_bore_52_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_52_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_52_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_52_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_52_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_52_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_52_cgen = stim_wide[0];
    assign sigFromSrams_bore_53_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_53_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_53_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_53_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_53_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_53_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_53_cgen = stim_wide[0];
    assign sigFromSrams_bore_54_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_54_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_54_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_54_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_54_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_54_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_54_cgen = stim_wide[0];
    assign sigFromSrams_bore_55_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_55_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_55_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_55_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_55_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_55_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_55_cgen = stim_wide[0];
    assign sigFromSrams_bore_56_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_56_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_56_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_56_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_56_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_56_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_56_cgen = stim_wide[0];
    assign sigFromSrams_bore_57_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_57_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_57_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_57_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_57_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_57_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_57_cgen = stim_wide[0];
    assign sigFromSrams_bore_58_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_58_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_58_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_58_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_58_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_58_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_58_cgen = stim_wide[0];
    assign sigFromSrams_bore_59_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_59_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_59_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_59_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_59_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_59_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_59_cgen = stim_wide[0];
    assign sigFromSrams_bore_60_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_60_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_60_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_60_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_60_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_60_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_60_cgen = stim_wide[0];
    assign sigFromSrams_bore_61_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_61_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_61_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_61_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_61_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_61_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_61_cgen = stim_wide[0];
    assign sigFromSrams_bore_62_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_62_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_62_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_62_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_62_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_62_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_62_cgen = stim_wide[0];
    assign sigFromSrams_bore_63_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_63_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_63_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_63_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_63_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_63_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_63_cgen = stim_wide[0];
    assign sigFromSrams_bore_64_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_64_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_64_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_64_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_64_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_64_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_64_cgen = stim_wide[0];
    assign sigFromSrams_bore_65_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_65_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_65_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_65_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_65_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_65_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_65_cgen = stim_wide[0];
    assign sigFromSrams_bore_66_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_66_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_66_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_66_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_66_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_66_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_66_cgen = stim_wide[0];
    assign sigFromSrams_bore_67_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_67_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_67_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_67_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_67_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_67_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_67_cgen = stim_wide[0];
    assign sigFromSrams_bore_68_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_68_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_68_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_68_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_68_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_68_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_68_cgen = stim_wide[0];
    assign sigFromSrams_bore_69_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_69_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_69_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_69_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_69_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_69_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_69_cgen = stim_wide[0];
    assign sigFromSrams_bore_70_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_70_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_70_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_70_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_70_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_70_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_70_cgen = stim_wide[0];
    assign sigFromSrams_bore_71_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_71_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_71_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_71_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_71_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_71_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_71_cgen = stim_wide[0];
    assign sigFromSrams_bore_72_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_72_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_72_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_72_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_72_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_72_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_72_cgen = stim_wide[0];
    assign sigFromSrams_bore_73_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_73_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_73_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_73_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_73_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_73_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_73_cgen = stim_wide[0];
    assign sigFromSrams_bore_74_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_74_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_74_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_74_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_74_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_74_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_74_cgen = stim_wide[0];
    assign sigFromSrams_bore_75_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_75_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_75_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_75_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_75_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_75_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_75_cgen = stim_wide[0];
    assign sigFromSrams_bore_76_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_76_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_76_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_76_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_76_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_76_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_76_cgen = stim_wide[0];
    assign sigFromSrams_bore_77_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_77_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_77_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_77_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_77_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_77_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_77_cgen = stim_wide[0];
    assign sigFromSrams_bore_78_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_78_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_78_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_78_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_78_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_78_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_78_cgen = stim_wide[0];
    assign sigFromSrams_bore_79_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_79_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_79_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_79_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_79_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_79_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_79_cgen = stim_wide[0];
    assign sigFromSrams_bore_80_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_80_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_80_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_80_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_80_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_80_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_80_cgen = stim_wide[0];
    assign sigFromSrams_bore_81_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_81_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_81_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_81_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_81_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_81_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_81_cgen = stim_wide[0];
    assign sigFromSrams_bore_82_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_82_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_82_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_82_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_82_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_82_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_82_cgen = stim_wide[0];
    assign sigFromSrams_bore_83_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_83_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_83_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_83_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_83_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_83_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_83_cgen = stim_wide[0];
    assign sigFromSrams_bore_84_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_84_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_84_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_84_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_84_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_84_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_84_cgen = stim_wide[0];
    assign sigFromSrams_bore_85_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_85_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_85_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_85_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_85_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_85_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_85_cgen = stim_wide[0];
    assign sigFromSrams_bore_86_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_86_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_86_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_86_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_86_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_86_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_86_cgen = stim_wide[0];
    assign sigFromSrams_bore_87_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_87_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_87_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_87_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_87_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_87_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_87_cgen = stim_wide[0];
    assign sigFromSrams_bore_88_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_88_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_88_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_88_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_88_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_88_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_88_cgen = stim_wide[0];
    assign sigFromSrams_bore_89_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_89_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_89_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_89_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_89_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_89_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_89_cgen = stim_wide[0];
    assign sigFromSrams_bore_90_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_90_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_90_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_90_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_90_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_90_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_90_cgen = stim_wide[0];
    assign sigFromSrams_bore_91_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_91_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_91_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_91_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_91_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_91_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_91_cgen = stim_wide[0];
    assign sigFromSrams_bore_92_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_92_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_92_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_92_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_92_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_92_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_92_cgen = stim_wide[0];
    assign sigFromSrams_bore_93_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_93_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_93_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_93_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_93_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_93_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_93_cgen = stim_wide[0];
    assign sigFromSrams_bore_94_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_94_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_94_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_94_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_94_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_94_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_94_cgen = stim_wide[0];
    assign sigFromSrams_bore_95_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_95_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_95_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_95_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_95_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_95_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_95_cgen = stim_wide[0];
    assign sigFromSrams_bore_96_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_96_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_96_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_96_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_96_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_96_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_96_cgen = stim_wide[0];
    assign sigFromSrams_bore_97_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_97_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_97_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_97_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_97_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_97_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_97_cgen = stim_wide[0];
    assign sigFromSrams_bore_98_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_98_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_98_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_98_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_98_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_98_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_98_cgen = stim_wide[0];
    assign sigFromSrams_bore_99_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_99_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_99_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_99_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_99_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_99_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_99_cgen = stim_wide[0];
    assign sigFromSrams_bore_100_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_100_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_100_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_100_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_100_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_100_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_100_cgen = stim_wide[0];
    assign sigFromSrams_bore_101_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_101_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_101_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_101_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_101_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_101_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_101_cgen = stim_wide[0];
    assign sigFromSrams_bore_102_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_102_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_102_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_102_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_102_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_102_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_102_cgen = stim_wide[0];
    assign sigFromSrams_bore_103_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_103_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_103_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_103_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_103_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_103_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_103_cgen = stim_wide[0];
    assign sigFromSrams_bore_104_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_104_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_104_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_104_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_104_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_104_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_104_cgen = stim_wide[0];
    assign sigFromSrams_bore_105_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_105_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_105_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_105_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_105_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_105_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_105_cgen = stim_wide[0];
    assign sigFromSrams_bore_106_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_106_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_106_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_106_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_106_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_106_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_106_cgen = stim_wide[0];
    assign sigFromSrams_bore_107_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_107_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_107_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_107_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_107_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_107_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_107_cgen = stim_wide[0];
    assign sigFromSrams_bore_108_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_108_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_108_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_108_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_108_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_108_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_108_cgen = stim_wide[0];
    assign sigFromSrams_bore_109_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_109_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_109_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_109_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_109_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_109_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_109_cgen = stim_wide[0];
    assign sigFromSrams_bore_110_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_110_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_110_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_110_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_110_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_110_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_110_cgen = stim_wide[0];
    assign sigFromSrams_bore_111_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_111_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_111_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_111_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_111_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_111_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_111_cgen = stim_wide[0];
    assign sigFromSrams_bore_112_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_112_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_112_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_112_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_112_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_112_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_112_cgen = stim_wide[0];
    assign sigFromSrams_bore_113_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_113_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_113_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_113_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_113_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_113_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_113_cgen = stim_wide[0];
    assign sigFromSrams_bore_114_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_114_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_114_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_114_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_114_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_114_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_114_cgen = stim_wide[0];
    assign sigFromSrams_bore_115_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_115_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_115_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_115_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_115_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_115_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_115_cgen = stim_wide[0];
    assign sigFromSrams_bore_116_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_116_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_116_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_116_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_116_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_116_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_116_cgen = stim_wide[0];
    assign sigFromSrams_bore_117_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_117_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_117_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_117_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_117_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_117_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_117_cgen = stim_wide[0];
    assign sigFromSrams_bore_118_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_118_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_118_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_118_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_118_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_118_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_118_cgen = stim_wide[0];
    assign sigFromSrams_bore_119_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_119_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_119_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_119_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_119_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_119_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_119_cgen = stim_wide[0];
    assign sigFromSrams_bore_120_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_120_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_120_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_120_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_120_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_120_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_120_cgen = stim_wide[0];
    assign sigFromSrams_bore_121_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_121_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_121_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_121_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_121_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_121_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_121_cgen = stim_wide[0];
    assign sigFromSrams_bore_122_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_122_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_122_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_122_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_122_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_122_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_122_cgen = stim_wide[0];
    assign sigFromSrams_bore_123_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_123_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_123_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_123_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_123_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_123_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_123_cgen = stim_wide[0];
    assign sigFromSrams_bore_124_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_124_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_124_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_124_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_124_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_124_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_124_cgen = stim_wide[0];
    assign sigFromSrams_bore_125_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_125_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_125_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_125_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_125_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_125_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_125_cgen = stim_wide[0];
    assign sigFromSrams_bore_126_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_126_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_126_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_126_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_126_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_126_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_126_cgen = stim_wide[0];
    assign sigFromSrams_bore_127_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_127_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_127_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_127_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_127_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_127_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_127_cgen = stim_wide[0];
    assign sigFromSrams_bore_128_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_128_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_128_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_128_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_128_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_128_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_128_cgen = stim_wide[0];
    assign sigFromSrams_bore_129_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_129_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_129_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_129_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_129_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_129_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_129_cgen = stim_wide[0];
    assign sigFromSrams_bore_130_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_130_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_130_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_130_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_130_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_130_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_130_cgen = stim_wide[0];
    assign sigFromSrams_bore_131_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_131_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_131_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_131_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_131_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_131_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_131_cgen = stim_wide[0];
    assign sigFromSrams_bore_132_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_132_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_132_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_132_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_132_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_132_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_132_cgen = stim_wide[0];
    assign sigFromSrams_bore_133_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_133_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_133_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_133_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_133_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_133_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_133_cgen = stim_wide[0];
    assign sigFromSrams_bore_134_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_134_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_134_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_134_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_134_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_134_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_134_cgen = stim_wide[0];
    assign sigFromSrams_bore_135_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_135_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_135_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_135_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_135_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_135_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_135_cgen = stim_wide[0];
    assign sigFromSrams_bore_136_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_136_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_136_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_136_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_136_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_136_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_136_cgen = stim_wide[0];
    assign sigFromSrams_bore_137_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_137_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_137_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_137_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_137_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_137_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_137_cgen = stim_wide[0];
    assign sigFromSrams_bore_138_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_138_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_138_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_138_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_138_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_138_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_138_cgen = stim_wide[0];
    assign sigFromSrams_bore_139_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_139_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_139_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_139_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_139_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_139_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_139_cgen = stim_wide[0];
    assign sigFromSrams_bore_140_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_140_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_140_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_140_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_140_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_140_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_140_cgen = stim_wide[0];
    assign sigFromSrams_bore_141_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_141_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_141_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_141_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_141_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_141_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_141_cgen = stim_wide[0];
    assign sigFromSrams_bore_142_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_142_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_142_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_142_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_142_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_142_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_142_cgen = stim_wide[0];
    assign sigFromSrams_bore_143_ram_hold = stim_wide[0];
    assign sigFromSrams_bore_143_ram_bypass = stim_wide[0];
    assign sigFromSrams_bore_143_ram_bp_clken = stim_wide[0];
    assign sigFromSrams_bore_143_ram_aux_clk = stim_wide[0];
    assign sigFromSrams_bore_143_ram_aux_ckbp = stim_wide[0];
    assign sigFromSrams_bore_143_ram_mcp_hold = stim_wide[0];
    assign sigFromSrams_bore_143_cgen = stim_wide[0];
    assign io_toFtq_prediction_ready = 1'b1;

    Bpu dut (
        .clock(clk),
        .reset(reset),
        .io_ctrl_ubtbEnable(io_ctrl_ubtbEnable),
        .io_ctrl_abtbEnable(io_ctrl_abtbEnable),
        .io_ctrl_mbtbEnable(io_ctrl_mbtbEnable),
        .io_ctrl_tageEnable(io_ctrl_tageEnable),
        .io_ctrl_scEnable(io_ctrl_scEnable),
        .io_ctrl_ittageEnable(io_ctrl_ittageEnable),
        .io_resetVector_addr(io_resetVector_addr),
        .io_fromFtq_redirect_valid(io_fromFtq_redirect_valid),
        .io_fromFtq_redirect_bits_cfiPc_addr(io_fromFtq_redirect_bits_cfiPc_addr),
        .io_fromFtq_redirect_bits_target_addr(io_fromFtq_redirect_bits_target_addr),
        .io_fromFtq_redirect_bits_taken(io_fromFtq_redirect_bits_taken),
        .io_fromFtq_redirect_bits_attribute_branchType(io_fromFtq_redirect_bits_attribute_branchType),
        .io_fromFtq_redirect_bits_attribute_rasAction(io_fromFtq_redirect_bits_attribute_rasAction),
        .io_fromFtq_redirect_bits_meta_phr_phrPtr_flag(io_fromFtq_redirect_bits_meta_phr_phrPtr_flag),
        .io_fromFtq_redirect_bits_meta_phr_phrPtr_value(io_fromFtq_redirect_bits_meta_phr_phrPtr_value),
        .io_fromFtq_redirect_bits_meta_phr_phrLowBits(io_fromFtq_redirect_bits_meta_phr_phrLowBits),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_ghr(io_fromFtq_redirect_bits_meta_commonHRMeta_ghr),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_bw(io_fromFtq_redirect_bits_meta_commonHRMeta_bw),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_0(io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_0),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_1(io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_1),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_2(io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_2),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_3(io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_3),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_4(io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_4),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_5(io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_5),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_6(io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_6),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_7(io_fromFtq_redirect_bits_meta_commonHRMeta_hitMask_7),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_0_branchType(io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_0_branchType),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_1_branchType(io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_1_branchType),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_2_branchType(io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_2_branchType),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_3_branchType(io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_3_branchType),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_4_branchType(io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_4_branchType),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_5_branchType(io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_5_branchType),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_6_branchType(io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_6_branchType),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_7_branchType(io_fromFtq_redirect_bits_meta_commonHRMeta_attribute_7_branchType),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_position_0(io_fromFtq_redirect_bits_meta_commonHRMeta_position_0),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_position_1(io_fromFtq_redirect_bits_meta_commonHRMeta_position_1),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_position_2(io_fromFtq_redirect_bits_meta_commonHRMeta_position_2),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_position_3(io_fromFtq_redirect_bits_meta_commonHRMeta_position_3),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_position_4(io_fromFtq_redirect_bits_meta_commonHRMeta_position_4),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_position_5(io_fromFtq_redirect_bits_meta_commonHRMeta_position_5),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_position_6(io_fromFtq_redirect_bits_meta_commonHRMeta_position_6),
        .io_fromFtq_redirect_bits_meta_commonHRMeta_position_7(io_fromFtq_redirect_bits_meta_commonHRMeta_position_7),
        .io_fromFtq_redirect_bits_meta_ras_ssp(io_fromFtq_redirect_bits_meta_ras_ssp),
        .io_fromFtq_redirect_bits_meta_ras_sctr(io_fromFtq_redirect_bits_meta_ras_sctr),
        .io_fromFtq_redirect_bits_meta_ras_tosw_flag(io_fromFtq_redirect_bits_meta_ras_tosw_flag),
        .io_fromFtq_redirect_bits_meta_ras_tosw_value(io_fromFtq_redirect_bits_meta_ras_tosw_value),
        .io_fromFtq_redirect_bits_meta_ras_tosr_flag(io_fromFtq_redirect_bits_meta_ras_tosr_flag),
        .io_fromFtq_redirect_bits_meta_ras_tosr_value(io_fromFtq_redirect_bits_meta_ras_tosr_value),
        .io_fromFtq_redirect_bits_meta_ras_nos_flag(io_fromFtq_redirect_bits_meta_ras_nos_flag),
        .io_fromFtq_redirect_bits_meta_ras_nos_value(io_fromFtq_redirect_bits_meta_ras_nos_value),
        .io_fromFtq_train_ready(io_fromFtq_train_ready),
        .io_fromFtq_train_valid(io_fromFtq_train_valid),
        .io_fromFtq_train_bits_startPc_addr(io_fromFtq_train_bits_startPc_addr),
        .io_fromFtq_train_bits_branches_0_valid(io_fromFtq_train_bits_branches_0_valid),
        .io_fromFtq_train_bits_branches_0_bits_target_addr(io_fromFtq_train_bits_branches_0_bits_target_addr),
        .io_fromFtq_train_bits_branches_0_bits_taken(io_fromFtq_train_bits_branches_0_bits_taken),
        .io_fromFtq_train_bits_branches_0_bits_cfiPosition(io_fromFtq_train_bits_branches_0_bits_cfiPosition),
        .io_fromFtq_train_bits_branches_0_bits_attribute_branchType(io_fromFtq_train_bits_branches_0_bits_attribute_branchType),
        .io_fromFtq_train_bits_branches_0_bits_attribute_rasAction(io_fromFtq_train_bits_branches_0_bits_attribute_rasAction),
        .io_fromFtq_train_bits_branches_0_bits_mispredict(io_fromFtq_train_bits_branches_0_bits_mispredict),
        .io_fromFtq_train_bits_branches_1_valid(io_fromFtq_train_bits_branches_1_valid),
        .io_fromFtq_train_bits_branches_1_bits_target_addr(io_fromFtq_train_bits_branches_1_bits_target_addr),
        .io_fromFtq_train_bits_branches_1_bits_taken(io_fromFtq_train_bits_branches_1_bits_taken),
        .io_fromFtq_train_bits_branches_1_bits_cfiPosition(io_fromFtq_train_bits_branches_1_bits_cfiPosition),
        .io_fromFtq_train_bits_branches_1_bits_attribute_branchType(io_fromFtq_train_bits_branches_1_bits_attribute_branchType),
        .io_fromFtq_train_bits_branches_1_bits_attribute_rasAction(io_fromFtq_train_bits_branches_1_bits_attribute_rasAction),
        .io_fromFtq_train_bits_branches_1_bits_mispredict(io_fromFtq_train_bits_branches_1_bits_mispredict),
        .io_fromFtq_train_bits_branches_2_valid(io_fromFtq_train_bits_branches_2_valid),
        .io_fromFtq_train_bits_branches_2_bits_target_addr(io_fromFtq_train_bits_branches_2_bits_target_addr),
        .io_fromFtq_train_bits_branches_2_bits_taken(io_fromFtq_train_bits_branches_2_bits_taken),
        .io_fromFtq_train_bits_branches_2_bits_cfiPosition(io_fromFtq_train_bits_branches_2_bits_cfiPosition),
        .io_fromFtq_train_bits_branches_2_bits_attribute_branchType(io_fromFtq_train_bits_branches_2_bits_attribute_branchType),
        .io_fromFtq_train_bits_branches_2_bits_attribute_rasAction(io_fromFtq_train_bits_branches_2_bits_attribute_rasAction),
        .io_fromFtq_train_bits_branches_2_bits_mispredict(io_fromFtq_train_bits_branches_2_bits_mispredict),
        .io_fromFtq_train_bits_branches_3_valid(io_fromFtq_train_bits_branches_3_valid),
        .io_fromFtq_train_bits_branches_3_bits_target_addr(io_fromFtq_train_bits_branches_3_bits_target_addr),
        .io_fromFtq_train_bits_branches_3_bits_taken(io_fromFtq_train_bits_branches_3_bits_taken),
        .io_fromFtq_train_bits_branches_3_bits_cfiPosition(io_fromFtq_train_bits_branches_3_bits_cfiPosition),
        .io_fromFtq_train_bits_branches_3_bits_attribute_branchType(io_fromFtq_train_bits_branches_3_bits_attribute_branchType),
        .io_fromFtq_train_bits_branches_3_bits_attribute_rasAction(io_fromFtq_train_bits_branches_3_bits_attribute_rasAction),
        .io_fromFtq_train_bits_branches_3_bits_mispredict(io_fromFtq_train_bits_branches_3_bits_mispredict),
        .io_fromFtq_train_bits_branches_4_valid(io_fromFtq_train_bits_branches_4_valid),
        .io_fromFtq_train_bits_branches_4_bits_target_addr(io_fromFtq_train_bits_branches_4_bits_target_addr),
        .io_fromFtq_train_bits_branches_4_bits_taken(io_fromFtq_train_bits_branches_4_bits_taken),
        .io_fromFtq_train_bits_branches_4_bits_cfiPosition(io_fromFtq_train_bits_branches_4_bits_cfiPosition),
        .io_fromFtq_train_bits_branches_4_bits_attribute_branchType(io_fromFtq_train_bits_branches_4_bits_attribute_branchType),
        .io_fromFtq_train_bits_branches_4_bits_attribute_rasAction(io_fromFtq_train_bits_branches_4_bits_attribute_rasAction),
        .io_fromFtq_train_bits_branches_4_bits_mispredict(io_fromFtq_train_bits_branches_4_bits_mispredict),
        .io_fromFtq_train_bits_branches_5_valid(io_fromFtq_train_bits_branches_5_valid),
        .io_fromFtq_train_bits_branches_5_bits_target_addr(io_fromFtq_train_bits_branches_5_bits_target_addr),
        .io_fromFtq_train_bits_branches_5_bits_taken(io_fromFtq_train_bits_branches_5_bits_taken),
        .io_fromFtq_train_bits_branches_5_bits_cfiPosition(io_fromFtq_train_bits_branches_5_bits_cfiPosition),
        .io_fromFtq_train_bits_branches_5_bits_attribute_branchType(io_fromFtq_train_bits_branches_5_bits_attribute_branchType),
        .io_fromFtq_train_bits_branches_5_bits_attribute_rasAction(io_fromFtq_train_bits_branches_5_bits_attribute_rasAction),
        .io_fromFtq_train_bits_branches_5_bits_mispredict(io_fromFtq_train_bits_branches_5_bits_mispredict),
        .io_fromFtq_train_bits_branches_6_valid(io_fromFtq_train_bits_branches_6_valid),
        .io_fromFtq_train_bits_branches_6_bits_target_addr(io_fromFtq_train_bits_branches_6_bits_target_addr),
        .io_fromFtq_train_bits_branches_6_bits_taken(io_fromFtq_train_bits_branches_6_bits_taken),
        .io_fromFtq_train_bits_branches_6_bits_cfiPosition(io_fromFtq_train_bits_branches_6_bits_cfiPosition),
        .io_fromFtq_train_bits_branches_6_bits_attribute_branchType(io_fromFtq_train_bits_branches_6_bits_attribute_branchType),
        .io_fromFtq_train_bits_branches_6_bits_attribute_rasAction(io_fromFtq_train_bits_branches_6_bits_attribute_rasAction),
        .io_fromFtq_train_bits_branches_6_bits_mispredict(io_fromFtq_train_bits_branches_6_bits_mispredict),
        .io_fromFtq_train_bits_branches_7_valid(io_fromFtq_train_bits_branches_7_valid),
        .io_fromFtq_train_bits_branches_7_bits_target_addr(io_fromFtq_train_bits_branches_7_bits_target_addr),
        .io_fromFtq_train_bits_branches_7_bits_taken(io_fromFtq_train_bits_branches_7_bits_taken),
        .io_fromFtq_train_bits_branches_7_bits_cfiPosition(io_fromFtq_train_bits_branches_7_bits_cfiPosition),
        .io_fromFtq_train_bits_branches_7_bits_attribute_branchType(io_fromFtq_train_bits_branches_7_bits_attribute_branchType),
        .io_fromFtq_train_bits_branches_7_bits_attribute_rasAction(io_fromFtq_train_bits_branches_7_bits_attribute_rasAction),
        .io_fromFtq_train_bits_branches_7_bits_mispredict(io_fromFtq_train_bits_branches_7_bits_mispredict),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_0_rawHit(io_fromFtq_train_bits_meta_mbtb_entries_0_0_rawHit),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_0_position(io_fromFtq_train_bits_meta_mbtb_entries_0_0_position),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_branchType(io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_branchType),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_rasAction(io_fromFtq_train_bits_meta_mbtb_entries_0_0_attribute_rasAction),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_0_counter_value(io_fromFtq_train_bits_meta_mbtb_entries_0_0_counter_value),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_1_rawHit(io_fromFtq_train_bits_meta_mbtb_entries_0_1_rawHit),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_1_position(io_fromFtq_train_bits_meta_mbtb_entries_0_1_position),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_branchType(io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_branchType),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_rasAction(io_fromFtq_train_bits_meta_mbtb_entries_0_1_attribute_rasAction),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_1_counter_value(io_fromFtq_train_bits_meta_mbtb_entries_0_1_counter_value),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_2_rawHit(io_fromFtq_train_bits_meta_mbtb_entries_0_2_rawHit),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_2_position(io_fromFtq_train_bits_meta_mbtb_entries_0_2_position),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_branchType(io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_branchType),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_rasAction(io_fromFtq_train_bits_meta_mbtb_entries_0_2_attribute_rasAction),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_2_counter_value(io_fromFtq_train_bits_meta_mbtb_entries_0_2_counter_value),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_3_rawHit(io_fromFtq_train_bits_meta_mbtb_entries_0_3_rawHit),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_3_position(io_fromFtq_train_bits_meta_mbtb_entries_0_3_position),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_branchType(io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_branchType),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_rasAction(io_fromFtq_train_bits_meta_mbtb_entries_0_3_attribute_rasAction),
        .io_fromFtq_train_bits_meta_mbtb_entries_0_3_counter_value(io_fromFtq_train_bits_meta_mbtb_entries_0_3_counter_value),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_0_rawHit(io_fromFtq_train_bits_meta_mbtb_entries_1_0_rawHit),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_0_position(io_fromFtq_train_bits_meta_mbtb_entries_1_0_position),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_branchType(io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_branchType),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_rasAction(io_fromFtq_train_bits_meta_mbtb_entries_1_0_attribute_rasAction),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_0_counter_value(io_fromFtq_train_bits_meta_mbtb_entries_1_0_counter_value),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_1_rawHit(io_fromFtq_train_bits_meta_mbtb_entries_1_1_rawHit),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_1_position(io_fromFtq_train_bits_meta_mbtb_entries_1_1_position),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_branchType(io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_branchType),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_rasAction(io_fromFtq_train_bits_meta_mbtb_entries_1_1_attribute_rasAction),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_1_counter_value(io_fromFtq_train_bits_meta_mbtb_entries_1_1_counter_value),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_2_rawHit(io_fromFtq_train_bits_meta_mbtb_entries_1_2_rawHit),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_2_position(io_fromFtq_train_bits_meta_mbtb_entries_1_2_position),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_branchType(io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_branchType),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_rasAction(io_fromFtq_train_bits_meta_mbtb_entries_1_2_attribute_rasAction),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_2_counter_value(io_fromFtq_train_bits_meta_mbtb_entries_1_2_counter_value),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_3_rawHit(io_fromFtq_train_bits_meta_mbtb_entries_1_3_rawHit),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_3_position(io_fromFtq_train_bits_meta_mbtb_entries_1_3_position),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_branchType(io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_branchType),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_rasAction(io_fromFtq_train_bits_meta_mbtb_entries_1_3_attribute_rasAction),
        .io_fromFtq_train_bits_meta_mbtb_entries_1_3_counter_value(io_fromFtq_train_bits_meta_mbtb_entries_1_3_counter_value),
        .io_fromFtq_train_bits_meta_tage_entries_0_useProvider(io_fromFtq_train_bits_meta_tage_entries_0_useProvider),
        .io_fromFtq_train_bits_meta_tage_entries_0_providerTableIdx(io_fromFtq_train_bits_meta_tage_entries_0_providerTableIdx),
        .io_fromFtq_train_bits_meta_tage_entries_0_providerWayIdx(io_fromFtq_train_bits_meta_tage_entries_0_providerWayIdx),
        .io_fromFtq_train_bits_meta_tage_entries_0_providerTakenCtr_value(io_fromFtq_train_bits_meta_tage_entries_0_providerTakenCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_0_providerUsefulCtr_value(io_fromFtq_train_bits_meta_tage_entries_0_providerUsefulCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_0_altOrBasePred(io_fromFtq_train_bits_meta_tage_entries_0_altOrBasePred),
        .io_fromFtq_train_bits_meta_tage_entries_1_useProvider(io_fromFtq_train_bits_meta_tage_entries_1_useProvider),
        .io_fromFtq_train_bits_meta_tage_entries_1_providerTableIdx(io_fromFtq_train_bits_meta_tage_entries_1_providerTableIdx),
        .io_fromFtq_train_bits_meta_tage_entries_1_providerWayIdx(io_fromFtq_train_bits_meta_tage_entries_1_providerWayIdx),
        .io_fromFtq_train_bits_meta_tage_entries_1_providerTakenCtr_value(io_fromFtq_train_bits_meta_tage_entries_1_providerTakenCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_1_providerUsefulCtr_value(io_fromFtq_train_bits_meta_tage_entries_1_providerUsefulCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_1_altOrBasePred(io_fromFtq_train_bits_meta_tage_entries_1_altOrBasePred),
        .io_fromFtq_train_bits_meta_tage_entries_2_useProvider(io_fromFtq_train_bits_meta_tage_entries_2_useProvider),
        .io_fromFtq_train_bits_meta_tage_entries_2_providerTableIdx(io_fromFtq_train_bits_meta_tage_entries_2_providerTableIdx),
        .io_fromFtq_train_bits_meta_tage_entries_2_providerWayIdx(io_fromFtq_train_bits_meta_tage_entries_2_providerWayIdx),
        .io_fromFtq_train_bits_meta_tage_entries_2_providerTakenCtr_value(io_fromFtq_train_bits_meta_tage_entries_2_providerTakenCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_2_providerUsefulCtr_value(io_fromFtq_train_bits_meta_tage_entries_2_providerUsefulCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_2_altOrBasePred(io_fromFtq_train_bits_meta_tage_entries_2_altOrBasePred),
        .io_fromFtq_train_bits_meta_tage_entries_3_useProvider(io_fromFtq_train_bits_meta_tage_entries_3_useProvider),
        .io_fromFtq_train_bits_meta_tage_entries_3_providerTableIdx(io_fromFtq_train_bits_meta_tage_entries_3_providerTableIdx),
        .io_fromFtq_train_bits_meta_tage_entries_3_providerWayIdx(io_fromFtq_train_bits_meta_tage_entries_3_providerWayIdx),
        .io_fromFtq_train_bits_meta_tage_entries_3_providerTakenCtr_value(io_fromFtq_train_bits_meta_tage_entries_3_providerTakenCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_3_providerUsefulCtr_value(io_fromFtq_train_bits_meta_tage_entries_3_providerUsefulCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_3_altOrBasePred(io_fromFtq_train_bits_meta_tage_entries_3_altOrBasePred),
        .io_fromFtq_train_bits_meta_tage_entries_4_useProvider(io_fromFtq_train_bits_meta_tage_entries_4_useProvider),
        .io_fromFtq_train_bits_meta_tage_entries_4_providerTableIdx(io_fromFtq_train_bits_meta_tage_entries_4_providerTableIdx),
        .io_fromFtq_train_bits_meta_tage_entries_4_providerWayIdx(io_fromFtq_train_bits_meta_tage_entries_4_providerWayIdx),
        .io_fromFtq_train_bits_meta_tage_entries_4_providerTakenCtr_value(io_fromFtq_train_bits_meta_tage_entries_4_providerTakenCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_4_providerUsefulCtr_value(io_fromFtq_train_bits_meta_tage_entries_4_providerUsefulCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_4_altOrBasePred(io_fromFtq_train_bits_meta_tage_entries_4_altOrBasePred),
        .io_fromFtq_train_bits_meta_tage_entries_5_useProvider(io_fromFtq_train_bits_meta_tage_entries_5_useProvider),
        .io_fromFtq_train_bits_meta_tage_entries_5_providerTableIdx(io_fromFtq_train_bits_meta_tage_entries_5_providerTableIdx),
        .io_fromFtq_train_bits_meta_tage_entries_5_providerWayIdx(io_fromFtq_train_bits_meta_tage_entries_5_providerWayIdx),
        .io_fromFtq_train_bits_meta_tage_entries_5_providerTakenCtr_value(io_fromFtq_train_bits_meta_tage_entries_5_providerTakenCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_5_providerUsefulCtr_value(io_fromFtq_train_bits_meta_tage_entries_5_providerUsefulCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_5_altOrBasePred(io_fromFtq_train_bits_meta_tage_entries_5_altOrBasePred),
        .io_fromFtq_train_bits_meta_tage_entries_6_useProvider(io_fromFtq_train_bits_meta_tage_entries_6_useProvider),
        .io_fromFtq_train_bits_meta_tage_entries_6_providerTableIdx(io_fromFtq_train_bits_meta_tage_entries_6_providerTableIdx),
        .io_fromFtq_train_bits_meta_tage_entries_6_providerWayIdx(io_fromFtq_train_bits_meta_tage_entries_6_providerWayIdx),
        .io_fromFtq_train_bits_meta_tage_entries_6_providerTakenCtr_value(io_fromFtq_train_bits_meta_tage_entries_6_providerTakenCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_6_providerUsefulCtr_value(io_fromFtq_train_bits_meta_tage_entries_6_providerUsefulCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_6_altOrBasePred(io_fromFtq_train_bits_meta_tage_entries_6_altOrBasePred),
        .io_fromFtq_train_bits_meta_tage_entries_7_useProvider(io_fromFtq_train_bits_meta_tage_entries_7_useProvider),
        .io_fromFtq_train_bits_meta_tage_entries_7_providerTableIdx(io_fromFtq_train_bits_meta_tage_entries_7_providerTableIdx),
        .io_fromFtq_train_bits_meta_tage_entries_7_providerWayIdx(io_fromFtq_train_bits_meta_tage_entries_7_providerWayIdx),
        .io_fromFtq_train_bits_meta_tage_entries_7_providerTakenCtr_value(io_fromFtq_train_bits_meta_tage_entries_7_providerTakenCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_7_providerUsefulCtr_value(io_fromFtq_train_bits_meta_tage_entries_7_providerUsefulCtr_value),
        .io_fromFtq_train_bits_meta_tage_entries_7_altOrBasePred(io_fromFtq_train_bits_meta_tage_entries_7_altOrBasePred),
        .io_fromFtq_train_bits_meta_sc_scPathResp_0_0(io_fromFtq_train_bits_meta_sc_scPathResp_0_0),
        .io_fromFtq_train_bits_meta_sc_scPathResp_0_1(io_fromFtq_train_bits_meta_sc_scPathResp_0_1),
        .io_fromFtq_train_bits_meta_sc_scPathResp_0_2(io_fromFtq_train_bits_meta_sc_scPathResp_0_2),
        .io_fromFtq_train_bits_meta_sc_scPathResp_0_3(io_fromFtq_train_bits_meta_sc_scPathResp_0_3),
        .io_fromFtq_train_bits_meta_sc_scPathResp_0_4(io_fromFtq_train_bits_meta_sc_scPathResp_0_4),
        .io_fromFtq_train_bits_meta_sc_scPathResp_0_5(io_fromFtq_train_bits_meta_sc_scPathResp_0_5),
        .io_fromFtq_train_bits_meta_sc_scPathResp_0_6(io_fromFtq_train_bits_meta_sc_scPathResp_0_6),
        .io_fromFtq_train_bits_meta_sc_scPathResp_0_7(io_fromFtq_train_bits_meta_sc_scPathResp_0_7),
        .io_fromFtq_train_bits_meta_sc_scPathResp_1_0(io_fromFtq_train_bits_meta_sc_scPathResp_1_0),
        .io_fromFtq_train_bits_meta_sc_scPathResp_1_1(io_fromFtq_train_bits_meta_sc_scPathResp_1_1),
        .io_fromFtq_train_bits_meta_sc_scPathResp_1_2(io_fromFtq_train_bits_meta_sc_scPathResp_1_2),
        .io_fromFtq_train_bits_meta_sc_scPathResp_1_3(io_fromFtq_train_bits_meta_sc_scPathResp_1_3),
        .io_fromFtq_train_bits_meta_sc_scPathResp_1_4(io_fromFtq_train_bits_meta_sc_scPathResp_1_4),
        .io_fromFtq_train_bits_meta_sc_scPathResp_1_5(io_fromFtq_train_bits_meta_sc_scPathResp_1_5),
        .io_fromFtq_train_bits_meta_sc_scPathResp_1_6(io_fromFtq_train_bits_meta_sc_scPathResp_1_6),
        .io_fromFtq_train_bits_meta_sc_scPathResp_1_7(io_fromFtq_train_bits_meta_sc_scPathResp_1_7),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_0(io_fromFtq_train_bits_meta_sc_scBiasResp_0),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_1(io_fromFtq_train_bits_meta_sc_scBiasResp_1),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_2(io_fromFtq_train_bits_meta_sc_scBiasResp_2),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_3(io_fromFtq_train_bits_meta_sc_scBiasResp_3),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_4(io_fromFtq_train_bits_meta_sc_scBiasResp_4),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_5(io_fromFtq_train_bits_meta_sc_scBiasResp_5),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_6(io_fromFtq_train_bits_meta_sc_scBiasResp_6),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_7(io_fromFtq_train_bits_meta_sc_scBiasResp_7),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_8(io_fromFtq_train_bits_meta_sc_scBiasResp_8),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_9(io_fromFtq_train_bits_meta_sc_scBiasResp_9),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_10(io_fromFtq_train_bits_meta_sc_scBiasResp_10),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_11(io_fromFtq_train_bits_meta_sc_scBiasResp_11),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_12(io_fromFtq_train_bits_meta_sc_scBiasResp_12),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_13(io_fromFtq_train_bits_meta_sc_scBiasResp_13),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_14(io_fromFtq_train_bits_meta_sc_scBiasResp_14),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_15(io_fromFtq_train_bits_meta_sc_scBiasResp_15),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_16(io_fromFtq_train_bits_meta_sc_scBiasResp_16),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_17(io_fromFtq_train_bits_meta_sc_scBiasResp_17),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_18(io_fromFtq_train_bits_meta_sc_scBiasResp_18),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_19(io_fromFtq_train_bits_meta_sc_scBiasResp_19),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_20(io_fromFtq_train_bits_meta_sc_scBiasResp_20),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_21(io_fromFtq_train_bits_meta_sc_scBiasResp_21),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_22(io_fromFtq_train_bits_meta_sc_scBiasResp_22),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_23(io_fromFtq_train_bits_meta_sc_scBiasResp_23),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_24(io_fromFtq_train_bits_meta_sc_scBiasResp_24),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_25(io_fromFtq_train_bits_meta_sc_scBiasResp_25),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_26(io_fromFtq_train_bits_meta_sc_scBiasResp_26),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_27(io_fromFtq_train_bits_meta_sc_scBiasResp_27),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_28(io_fromFtq_train_bits_meta_sc_scBiasResp_28),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_29(io_fromFtq_train_bits_meta_sc_scBiasResp_29),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_30(io_fromFtq_train_bits_meta_sc_scBiasResp_30),
        .io_fromFtq_train_bits_meta_sc_scBiasResp_31(io_fromFtq_train_bits_meta_sc_scBiasResp_31),
        .io_fromFtq_train_bits_meta_sc_scBiasLowerBits_0(io_fromFtq_train_bits_meta_sc_scBiasLowerBits_0),
        .io_fromFtq_train_bits_meta_sc_scBiasLowerBits_1(io_fromFtq_train_bits_meta_sc_scBiasLowerBits_1),
        .io_fromFtq_train_bits_meta_sc_scBiasLowerBits_2(io_fromFtq_train_bits_meta_sc_scBiasLowerBits_2),
        .io_fromFtq_train_bits_meta_sc_scBiasLowerBits_3(io_fromFtq_train_bits_meta_sc_scBiasLowerBits_3),
        .io_fromFtq_train_bits_meta_sc_scBiasLowerBits_4(io_fromFtq_train_bits_meta_sc_scBiasLowerBits_4),
        .io_fromFtq_train_bits_meta_sc_scBiasLowerBits_5(io_fromFtq_train_bits_meta_sc_scBiasLowerBits_5),
        .io_fromFtq_train_bits_meta_sc_scBiasLowerBits_6(io_fromFtq_train_bits_meta_sc_scBiasLowerBits_6),
        .io_fromFtq_train_bits_meta_sc_scBiasLowerBits_7(io_fromFtq_train_bits_meta_sc_scBiasLowerBits_7),
        .io_fromFtq_train_bits_meta_sc_scCommonHR_valid(io_fromFtq_train_bits_meta_sc_scCommonHR_valid),
        .io_fromFtq_train_bits_meta_sc_scCommonHR_ghr(io_fromFtq_train_bits_meta_sc_scCommonHR_ghr),
        .io_fromFtq_train_bits_meta_sc_scCommonHR_bw(io_fromFtq_train_bits_meta_sc_scCommonHR_bw),
        .io_fromFtq_train_bits_meta_sc_scPred_0(io_fromFtq_train_bits_meta_sc_scPred_0),
        .io_fromFtq_train_bits_meta_sc_scPred_1(io_fromFtq_train_bits_meta_sc_scPred_1),
        .io_fromFtq_train_bits_meta_sc_scPred_2(io_fromFtq_train_bits_meta_sc_scPred_2),
        .io_fromFtq_train_bits_meta_sc_scPred_3(io_fromFtq_train_bits_meta_sc_scPred_3),
        .io_fromFtq_train_bits_meta_sc_scPred_4(io_fromFtq_train_bits_meta_sc_scPred_4),
        .io_fromFtq_train_bits_meta_sc_scPred_5(io_fromFtq_train_bits_meta_sc_scPred_5),
        .io_fromFtq_train_bits_meta_sc_scPred_6(io_fromFtq_train_bits_meta_sc_scPred_6),
        .io_fromFtq_train_bits_meta_sc_scPred_7(io_fromFtq_train_bits_meta_sc_scPred_7),
        .io_fromFtq_train_bits_meta_sc_tagePred_0(io_fromFtq_train_bits_meta_sc_tagePred_0),
        .io_fromFtq_train_bits_meta_sc_tagePred_1(io_fromFtq_train_bits_meta_sc_tagePred_1),
        .io_fromFtq_train_bits_meta_sc_tagePred_2(io_fromFtq_train_bits_meta_sc_tagePred_2),
        .io_fromFtq_train_bits_meta_sc_tagePred_3(io_fromFtq_train_bits_meta_sc_tagePred_3),
        .io_fromFtq_train_bits_meta_sc_tagePred_4(io_fromFtq_train_bits_meta_sc_tagePred_4),
        .io_fromFtq_train_bits_meta_sc_tagePred_5(io_fromFtq_train_bits_meta_sc_tagePred_5),
        .io_fromFtq_train_bits_meta_sc_tagePred_6(io_fromFtq_train_bits_meta_sc_tagePred_6),
        .io_fromFtq_train_bits_meta_sc_tagePred_7(io_fromFtq_train_bits_meta_sc_tagePred_7),
        .io_fromFtq_train_bits_meta_sc_tagePredValid_0(io_fromFtq_train_bits_meta_sc_tagePredValid_0),
        .io_fromFtq_train_bits_meta_sc_tagePredValid_1(io_fromFtq_train_bits_meta_sc_tagePredValid_1),
        .io_fromFtq_train_bits_meta_sc_tagePredValid_2(io_fromFtq_train_bits_meta_sc_tagePredValid_2),
        .io_fromFtq_train_bits_meta_sc_tagePredValid_3(io_fromFtq_train_bits_meta_sc_tagePredValid_3),
        .io_fromFtq_train_bits_meta_sc_tagePredValid_4(io_fromFtq_train_bits_meta_sc_tagePredValid_4),
        .io_fromFtq_train_bits_meta_sc_tagePredValid_5(io_fromFtq_train_bits_meta_sc_tagePredValid_5),
        .io_fromFtq_train_bits_meta_sc_tagePredValid_6(io_fromFtq_train_bits_meta_sc_tagePredValid_6),
        .io_fromFtq_train_bits_meta_sc_tagePredValid_7(io_fromFtq_train_bits_meta_sc_tagePredValid_7),
        .io_fromFtq_train_bits_meta_sc_useScPred_0(io_fromFtq_train_bits_meta_sc_useScPred_0),
        .io_fromFtq_train_bits_meta_sc_useScPred_1(io_fromFtq_train_bits_meta_sc_useScPred_1),
        .io_fromFtq_train_bits_meta_sc_useScPred_2(io_fromFtq_train_bits_meta_sc_useScPred_2),
        .io_fromFtq_train_bits_meta_sc_useScPred_3(io_fromFtq_train_bits_meta_sc_useScPred_3),
        .io_fromFtq_train_bits_meta_sc_useScPred_4(io_fromFtq_train_bits_meta_sc_useScPred_4),
        .io_fromFtq_train_bits_meta_sc_useScPred_5(io_fromFtq_train_bits_meta_sc_useScPred_5),
        .io_fromFtq_train_bits_meta_sc_useScPred_6(io_fromFtq_train_bits_meta_sc_useScPred_6),
        .io_fromFtq_train_bits_meta_sc_useScPred_7(io_fromFtq_train_bits_meta_sc_useScPred_7),
        .io_fromFtq_train_bits_meta_sc_sumAboveThres_0(io_fromFtq_train_bits_meta_sc_sumAboveThres_0),
        .io_fromFtq_train_bits_meta_sc_sumAboveThres_1(io_fromFtq_train_bits_meta_sc_sumAboveThres_1),
        .io_fromFtq_train_bits_meta_sc_sumAboveThres_2(io_fromFtq_train_bits_meta_sc_sumAboveThres_2),
        .io_fromFtq_train_bits_meta_sc_sumAboveThres_3(io_fromFtq_train_bits_meta_sc_sumAboveThres_3),
        .io_fromFtq_train_bits_meta_sc_sumAboveThres_4(io_fromFtq_train_bits_meta_sc_sumAboveThres_4),
        .io_fromFtq_train_bits_meta_sc_sumAboveThres_5(io_fromFtq_train_bits_meta_sc_sumAboveThres_5),
        .io_fromFtq_train_bits_meta_sc_sumAboveThres_6(io_fromFtq_train_bits_meta_sc_sumAboveThres_6),
        .io_fromFtq_train_bits_meta_sc_sumAboveThres_7(io_fromFtq_train_bits_meta_sc_sumAboveThres_7),
        .io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_0(io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_0),
        .io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_1(io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_1),
        .io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_2(io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_2),
        .io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_3(io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_3),
        .io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_4(io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_4),
        .io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_5(io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_5),
        .io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_6(io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_6),
        .io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_7(io_fromFtq_train_bits_meta_sc_debug_scPathTakenVec_7),
        .io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_0(io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_0),
        .io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_1(io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_1),
        .io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_2(io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_2),
        .io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_3(io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_3),
        .io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_4(io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_4),
        .io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_5(io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_5),
        .io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_6(io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_6),
        .io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_7(io_fromFtq_train_bits_meta_sc_debug_scBiasTakenVec_7),
        .io_fromFtq_train_bits_meta_sc_debug_predPathIdx_0(io_fromFtq_train_bits_meta_sc_debug_predPathIdx_0),
        .io_fromFtq_train_bits_meta_sc_debug_predPathIdx_1(io_fromFtq_train_bits_meta_sc_debug_predPathIdx_1),
        .io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_0(io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_0),
        .io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_1(io_fromFtq_train_bits_meta_sc_debug_predGlobalIdx_1),
        .io_fromFtq_train_bits_meta_sc_debug_predBWIdx_0(io_fromFtq_train_bits_meta_sc_debug_predBWIdx_0),
        .io_fromFtq_train_bits_meta_sc_debug_predBWIdx_1(io_fromFtq_train_bits_meta_sc_debug_predBWIdx_1),
        .io_fromFtq_train_bits_meta_sc_debug_predBiasIdx(io_fromFtq_train_bits_meta_sc_debug_predBiasIdx),
        .io_fromFtq_train_bits_meta_ittage_provider_valid(io_fromFtq_train_bits_meta_ittage_provider_valid),
        .io_fromFtq_train_bits_meta_ittage_provider_bits(io_fromFtq_train_bits_meta_ittage_provider_bits),
        .io_fromFtq_train_bits_meta_ittage_altProvider_valid(io_fromFtq_train_bits_meta_ittage_altProvider_valid),
        .io_fromFtq_train_bits_meta_ittage_altProvider_bits(io_fromFtq_train_bits_meta_ittage_altProvider_bits),
        .io_fromFtq_train_bits_meta_ittage_altDiffers(io_fromFtq_train_bits_meta_ittage_altDiffers),
        .io_fromFtq_train_bits_meta_ittage_providerUsefulCnt_value(io_fromFtq_train_bits_meta_ittage_providerUsefulCnt_value),
        .io_fromFtq_train_bits_meta_ittage_providerCnt_value(io_fromFtq_train_bits_meta_ittage_providerCnt_value),
        .io_fromFtq_train_bits_meta_ittage_altProviderCnt_value(io_fromFtq_train_bits_meta_ittage_altProviderCnt_value),
        .io_fromFtq_train_bits_meta_ittage_allocate_valid(io_fromFtq_train_bits_meta_ittage_allocate_valid),
        .io_fromFtq_train_bits_meta_ittage_allocate_bits(io_fromFtq_train_bits_meta_ittage_allocate_bits),
        .io_fromFtq_train_bits_meta_ittage_providerTarget_addr(io_fromFtq_train_bits_meta_ittage_providerTarget_addr),
        .io_fromFtq_train_bits_meta_ittage_altProviderTarget_addr(io_fromFtq_train_bits_meta_ittage_altProviderTarget_addr),
        .io_fromFtq_train_bits_meta_phr_phrPtr_value(io_fromFtq_train_bits_meta_phr_phrPtr_value),
        .io_fromFtq_train_bits_meta_phr_phrLowBits(io_fromFtq_train_bits_meta_phr_phrLowBits),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_31_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_30_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_29_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_28_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_27_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_26_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_25_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_24_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_23_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_22_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_21_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_20_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_19_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_18_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_17_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_16_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_15_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_14_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_13_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_12_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_11_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_10_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_9_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_8_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_7_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_6_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_5_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_4_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_3_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_2_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_1_foldedHist),
        .io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist(io_fromFtq_train_bits_meta_phr_predFoldedHist_hist_0_foldedHist),
        .io_fromFtq_commit_valid(io_fromFtq_commit_valid),
        .io_fromFtq_commit_bits_meta_ras_ssp(io_fromFtq_commit_bits_meta_ras_ssp),
        .io_fromFtq_commit_bits_meta_ras_tosw_flag(io_fromFtq_commit_bits_meta_ras_tosw_flag),
        .io_fromFtq_commit_bits_meta_ras_tosw_value(io_fromFtq_commit_bits_meta_ras_tosw_value),
        .io_fromFtq_commit_bits_attribute_rasAction(io_fromFtq_commit_bits_attribute_rasAction),
        .io_fromFtq_bpuPtr_flag(io_fromFtq_bpuPtr_flag),
        .io_fromFtq_bpuPtr_value(io_fromFtq_bpuPtr_value),
        .io_toFtq_prediction_ready(io_toFtq_prediction_ready),
        .io_toFtq_prediction_valid(io_toFtq_prediction_valid),
        .io_toFtq_prediction_bits_startPc_addr(io_toFtq_prediction_bits_startPc_addr),
        .io_toFtq_prediction_bits_target_addr(io_toFtq_prediction_bits_target_addr),
        .io_toFtq_prediction_bits_takenCfiOffset_valid(io_toFtq_prediction_bits_takenCfiOffset_valid),
        .io_toFtq_prediction_bits_takenCfiOffset_bits(io_toFtq_prediction_bits_takenCfiOffset_bits),
        .io_toFtq_prediction_bits_s3Override(io_toFtq_prediction_bits_s3Override),
        .io_toFtq_meta_valid(io_toFtq_meta_valid),
        .io_toFtq_meta_bits_redirectMeta_phr_phrPtr_flag(io_toFtq_meta_bits_redirectMeta_phr_phrPtr_flag),
        .io_toFtq_meta_bits_redirectMeta_phr_phrPtr_value(io_toFtq_meta_bits_redirectMeta_phr_phrPtr_value),
        .io_toFtq_meta_bits_redirectMeta_phr_phrLowBits(io_toFtq_meta_bits_redirectMeta_phr_phrLowBits),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_ghr(io_toFtq_meta_bits_redirectMeta_commonHRMeta_ghr),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_bw(io_toFtq_meta_bits_redirectMeta_commonHRMeta_bw),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_0(io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_0),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_1(io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_1),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_2(io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_2),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_3(io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_3),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_4(io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_4),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_5(io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_5),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_6(io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_6),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_7(io_toFtq_meta_bits_redirectMeta_commonHRMeta_hitMask_7),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_0_branchType(io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_0_branchType),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_1_branchType(io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_1_branchType),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_2_branchType(io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_2_branchType),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_3_branchType(io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_3_branchType),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_4_branchType(io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_4_branchType),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_5_branchType(io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_5_branchType),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_6_branchType(io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_6_branchType),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_7_branchType(io_toFtq_meta_bits_redirectMeta_commonHRMeta_attribute_7_branchType),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_0(io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_0),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_1(io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_1),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_2(io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_2),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_3(io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_3),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_4(io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_4),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_5(io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_5),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_6(io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_6),
        .io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_7(io_toFtq_meta_bits_redirectMeta_commonHRMeta_position_7),
        .io_toFtq_meta_bits_redirectMeta_ras_ssp(io_toFtq_meta_bits_redirectMeta_ras_ssp),
        .io_toFtq_meta_bits_redirectMeta_ras_sctr(io_toFtq_meta_bits_redirectMeta_ras_sctr),
        .io_toFtq_meta_bits_redirectMeta_ras_tosw_flag(io_toFtq_meta_bits_redirectMeta_ras_tosw_flag),
        .io_toFtq_meta_bits_redirectMeta_ras_tosw_value(io_toFtq_meta_bits_redirectMeta_ras_tosw_value),
        .io_toFtq_meta_bits_redirectMeta_ras_tosr_flag(io_toFtq_meta_bits_redirectMeta_ras_tosr_flag),
        .io_toFtq_meta_bits_redirectMeta_ras_tosr_value(io_toFtq_meta_bits_redirectMeta_ras_tosr_value),
        .io_toFtq_meta_bits_redirectMeta_ras_nos_flag(io_toFtq_meta_bits_redirectMeta_ras_nos_flag),
        .io_toFtq_meta_bits_redirectMeta_ras_nos_value(io_toFtq_meta_bits_redirectMeta_ras_nos_value),
        .io_toFtq_meta_bits_redirectMeta_ras_topRetAddr_addr(io_toFtq_meta_bits_redirectMeta_ras_topRetAddr_addr),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_rawHit(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_rawHit),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_position(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_position),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_branchType(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_branchType),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_rasAction(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_attribute_rasAction),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_counter_value(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_0_counter_value),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_rawHit(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_rawHit),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_position(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_position),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_branchType(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_branchType),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_rasAction(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_attribute_rasAction),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_counter_value(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_1_counter_value),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_rawHit(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_rawHit),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_position(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_position),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_branchType(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_branchType),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_rasAction(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_attribute_rasAction),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_counter_value(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_2_counter_value),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_rawHit(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_rawHit),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_position(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_position),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_branchType(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_branchType),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_rasAction(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_attribute_rasAction),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_counter_value(io_toFtq_meta_bits_resolveMeta_mbtb_entries_0_3_counter_value),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_rawHit(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_rawHit),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_position(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_position),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_branchType(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_branchType),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_rasAction(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_attribute_rasAction),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_counter_value(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_0_counter_value),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_rawHit(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_rawHit),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_position(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_position),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_branchType(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_branchType),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_rasAction(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_attribute_rasAction),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_counter_value(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_1_counter_value),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_rawHit(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_rawHit),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_position(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_position),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_branchType(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_branchType),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_rasAction(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_attribute_rasAction),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_counter_value(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_2_counter_value),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_rawHit(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_rawHit),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_position(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_position),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_branchType(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_branchType),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_rasAction(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_attribute_rasAction),
        .io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_counter_value(io_toFtq_meta_bits_resolveMeta_mbtb_entries_1_3_counter_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_0_useProvider(io_toFtq_meta_bits_resolveMeta_tage_entries_0_useProvider),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerTableIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerTableIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerWayIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerWayIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerTakenCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerTakenCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerUsefulCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_0_providerUsefulCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_0_altOrBasePred(io_toFtq_meta_bits_resolveMeta_tage_entries_0_altOrBasePred),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_1_useProvider(io_toFtq_meta_bits_resolveMeta_tage_entries_1_useProvider),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerTableIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerTableIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerWayIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerWayIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerTakenCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerTakenCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerUsefulCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_1_providerUsefulCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_1_altOrBasePred(io_toFtq_meta_bits_resolveMeta_tage_entries_1_altOrBasePred),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_2_useProvider(io_toFtq_meta_bits_resolveMeta_tage_entries_2_useProvider),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerTableIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerTableIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerWayIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerWayIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerTakenCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerTakenCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerUsefulCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_2_providerUsefulCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_2_altOrBasePred(io_toFtq_meta_bits_resolveMeta_tage_entries_2_altOrBasePred),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_3_useProvider(io_toFtq_meta_bits_resolveMeta_tage_entries_3_useProvider),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerTableIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerTableIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerWayIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerWayIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerTakenCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerTakenCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerUsefulCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_3_providerUsefulCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_3_altOrBasePred(io_toFtq_meta_bits_resolveMeta_tage_entries_3_altOrBasePred),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_4_useProvider(io_toFtq_meta_bits_resolveMeta_tage_entries_4_useProvider),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerTableIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerTableIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerWayIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerWayIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerTakenCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerTakenCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerUsefulCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_4_providerUsefulCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_4_altOrBasePred(io_toFtq_meta_bits_resolveMeta_tage_entries_4_altOrBasePred),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_5_useProvider(io_toFtq_meta_bits_resolveMeta_tage_entries_5_useProvider),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerTableIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerTableIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerWayIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerWayIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerTakenCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerTakenCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerUsefulCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_5_providerUsefulCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_5_altOrBasePred(io_toFtq_meta_bits_resolveMeta_tage_entries_5_altOrBasePred),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_6_useProvider(io_toFtq_meta_bits_resolveMeta_tage_entries_6_useProvider),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerTableIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerTableIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerWayIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerWayIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerTakenCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerTakenCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerUsefulCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_6_providerUsefulCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_6_altOrBasePred(io_toFtq_meta_bits_resolveMeta_tage_entries_6_altOrBasePred),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_7_useProvider(io_toFtq_meta_bits_resolveMeta_tage_entries_7_useProvider),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerTableIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerTableIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerWayIdx(io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerWayIdx),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerTakenCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerTakenCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerUsefulCtr_value(io_toFtq_meta_bits_resolveMeta_tage_entries_7_providerUsefulCtr_value),
        .io_toFtq_meta_bits_resolveMeta_tage_entries_7_altOrBasePred(io_toFtq_meta_bits_resolveMeta_tage_entries_7_altOrBasePred),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_0(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_0),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_1(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_1),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_2(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_2),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_3(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_3),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_4(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_4),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_5(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_5),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_6(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_6),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_7(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_0_7),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_0(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_0),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_1(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_1),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_2(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_2),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_3(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_3),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_4(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_4),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_5(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_5),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_6(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_6),
        .io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_7(io_toFtq_meta_bits_resolveMeta_sc_scPathResp_1_7),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_0(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_0),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_1(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_1),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_2(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_2),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_3(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_3),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_4(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_4),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_5(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_5),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_6(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_6),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_7(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_7),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_8(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_8),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_9(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_9),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_10(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_10),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_11(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_11),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_12(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_12),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_13(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_13),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_14(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_14),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_15(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_15),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_16(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_16),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_17(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_17),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_18(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_18),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_19(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_19),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_20(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_20),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_21(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_21),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_22(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_22),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_23(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_23),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_24(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_24),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_25(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_25),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_26(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_26),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_27(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_27),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_28(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_28),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_29(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_29),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_30(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_30),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_31(io_toFtq_meta_bits_resolveMeta_sc_scBiasResp_31),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_0(io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_0),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_1(io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_1),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_2(io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_2),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_3(io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_3),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_4(io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_4),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_5(io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_5),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_6(io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_6),
        .io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_7(io_toFtq_meta_bits_resolveMeta_sc_scBiasLowerBits_7),
        .io_toFtq_meta_bits_resolveMeta_sc_scCommonHR_valid(io_toFtq_meta_bits_resolveMeta_sc_scCommonHR_valid),
        .io_toFtq_meta_bits_resolveMeta_sc_scCommonHR_ghr(io_toFtq_meta_bits_resolveMeta_sc_scCommonHR_ghr),
        .io_toFtq_meta_bits_resolveMeta_sc_scCommonHR_bw(io_toFtq_meta_bits_resolveMeta_sc_scCommonHR_bw),
        .io_toFtq_meta_bits_resolveMeta_sc_scPred_0(io_toFtq_meta_bits_resolveMeta_sc_scPred_0),
        .io_toFtq_meta_bits_resolveMeta_sc_scPred_1(io_toFtq_meta_bits_resolveMeta_sc_scPred_1),
        .io_toFtq_meta_bits_resolveMeta_sc_scPred_2(io_toFtq_meta_bits_resolveMeta_sc_scPred_2),
        .io_toFtq_meta_bits_resolveMeta_sc_scPred_3(io_toFtq_meta_bits_resolveMeta_sc_scPred_3),
        .io_toFtq_meta_bits_resolveMeta_sc_scPred_4(io_toFtq_meta_bits_resolveMeta_sc_scPred_4),
        .io_toFtq_meta_bits_resolveMeta_sc_scPred_5(io_toFtq_meta_bits_resolveMeta_sc_scPred_5),
        .io_toFtq_meta_bits_resolveMeta_sc_scPred_6(io_toFtq_meta_bits_resolveMeta_sc_scPred_6),
        .io_toFtq_meta_bits_resolveMeta_sc_scPred_7(io_toFtq_meta_bits_resolveMeta_sc_scPred_7),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePred_0(io_toFtq_meta_bits_resolveMeta_sc_tagePred_0),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePred_1(io_toFtq_meta_bits_resolveMeta_sc_tagePred_1),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePred_2(io_toFtq_meta_bits_resolveMeta_sc_tagePred_2),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePred_3(io_toFtq_meta_bits_resolveMeta_sc_tagePred_3),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePred_4(io_toFtq_meta_bits_resolveMeta_sc_tagePred_4),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePred_5(io_toFtq_meta_bits_resolveMeta_sc_tagePred_5),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePred_6(io_toFtq_meta_bits_resolveMeta_sc_tagePred_6),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePred_7(io_toFtq_meta_bits_resolveMeta_sc_tagePred_7),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_0(io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_0),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_1(io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_1),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_2(io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_2),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_3(io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_3),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_4(io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_4),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_5(io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_5),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_6(io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_6),
        .io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_7(io_toFtq_meta_bits_resolveMeta_sc_tagePredValid_7),
        .io_toFtq_meta_bits_resolveMeta_sc_useScPred_0(io_toFtq_meta_bits_resolveMeta_sc_useScPred_0),
        .io_toFtq_meta_bits_resolveMeta_sc_useScPred_1(io_toFtq_meta_bits_resolveMeta_sc_useScPred_1),
        .io_toFtq_meta_bits_resolveMeta_sc_useScPred_2(io_toFtq_meta_bits_resolveMeta_sc_useScPred_2),
        .io_toFtq_meta_bits_resolveMeta_sc_useScPred_3(io_toFtq_meta_bits_resolveMeta_sc_useScPred_3),
        .io_toFtq_meta_bits_resolveMeta_sc_useScPred_4(io_toFtq_meta_bits_resolveMeta_sc_useScPred_4),
        .io_toFtq_meta_bits_resolveMeta_sc_useScPred_5(io_toFtq_meta_bits_resolveMeta_sc_useScPred_5),
        .io_toFtq_meta_bits_resolveMeta_sc_useScPred_6(io_toFtq_meta_bits_resolveMeta_sc_useScPred_6),
        .io_toFtq_meta_bits_resolveMeta_sc_useScPred_7(io_toFtq_meta_bits_resolveMeta_sc_useScPred_7),
        .io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_0(io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_0),
        .io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_1(io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_1),
        .io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_2(io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_2),
        .io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_3(io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_3),
        .io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_4(io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_4),
        .io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_5(io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_5),
        .io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_6(io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_6),
        .io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_7(io_toFtq_meta_bits_resolveMeta_sc_sumAboveThres_7),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_0(io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_0),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_1(io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_1),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_2(io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_2),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_3(io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_3),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_4(io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_4),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_5(io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_5),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_6(io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_6),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_7(io_toFtq_meta_bits_resolveMeta_sc_debug_scPathTakenVec_7),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_0(io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_0),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_1(io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_1),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_2(io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_2),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_3(io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_3),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_4(io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_4),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_5(io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_5),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_6(io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_6),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_7(io_toFtq_meta_bits_resolveMeta_sc_debug_scBiasTakenVec_7),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_predPathIdx_0(io_toFtq_meta_bits_resolveMeta_sc_debug_predPathIdx_0),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_predPathIdx_1(io_toFtq_meta_bits_resolveMeta_sc_debug_predPathIdx_1),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_predGlobalIdx_0(io_toFtq_meta_bits_resolveMeta_sc_debug_predGlobalIdx_0),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_predGlobalIdx_1(io_toFtq_meta_bits_resolveMeta_sc_debug_predGlobalIdx_1),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_predBWIdx_0(io_toFtq_meta_bits_resolveMeta_sc_debug_predBWIdx_0),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_predBWIdx_1(io_toFtq_meta_bits_resolveMeta_sc_debug_predBWIdx_1),
        .io_toFtq_meta_bits_resolveMeta_sc_debug_predBiasIdx(io_toFtq_meta_bits_resolveMeta_sc_debug_predBiasIdx),
        .io_toFtq_meta_bits_resolveMeta_ittage_provider_valid(io_toFtq_meta_bits_resolveMeta_ittage_provider_valid),
        .io_toFtq_meta_bits_resolveMeta_ittage_provider_bits(io_toFtq_meta_bits_resolveMeta_ittage_provider_bits),
        .io_toFtq_meta_bits_resolveMeta_ittage_altProvider_valid(io_toFtq_meta_bits_resolveMeta_ittage_altProvider_valid),
        .io_toFtq_meta_bits_resolveMeta_ittage_altProvider_bits(io_toFtq_meta_bits_resolveMeta_ittage_altProvider_bits),
        .io_toFtq_meta_bits_resolveMeta_ittage_altDiffers(io_toFtq_meta_bits_resolveMeta_ittage_altDiffers),
        .io_toFtq_meta_bits_resolveMeta_ittage_providerUsefulCnt_value(io_toFtq_meta_bits_resolveMeta_ittage_providerUsefulCnt_value),
        .io_toFtq_meta_bits_resolveMeta_ittage_providerCnt_value(io_toFtq_meta_bits_resolveMeta_ittage_providerCnt_value),
        .io_toFtq_meta_bits_resolveMeta_ittage_altProviderCnt_value(io_toFtq_meta_bits_resolveMeta_ittage_altProviderCnt_value),
        .io_toFtq_meta_bits_resolveMeta_ittage_allocate_valid(io_toFtq_meta_bits_resolveMeta_ittage_allocate_valid),
        .io_toFtq_meta_bits_resolveMeta_ittage_allocate_bits(io_toFtq_meta_bits_resolveMeta_ittage_allocate_bits),
        .io_toFtq_meta_bits_resolveMeta_ittage_providerTarget_addr(io_toFtq_meta_bits_resolveMeta_ittage_providerTarget_addr),
        .io_toFtq_meta_bits_resolveMeta_ittage_altProviderTarget_addr(io_toFtq_meta_bits_resolveMeta_ittage_altProviderTarget_addr),
        .io_toFtq_meta_bits_resolveMeta_phr_phrPtr_value(io_toFtq_meta_bits_resolveMeta_phr_phrPtr_value),
        .io_toFtq_meta_bits_resolveMeta_phr_phrLowBits(io_toFtq_meta_bits_resolveMeta_phr_phrLowBits),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_31_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_31_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_30_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_30_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_29_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_29_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_28_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_28_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_27_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_27_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_26_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_26_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_25_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_25_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_24_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_24_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_23_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_23_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_22_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_22_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_21_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_21_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_20_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_20_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_19_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_19_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_18_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_18_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_17_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_17_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_16_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_16_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_15_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_15_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_14_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_14_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_13_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_13_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_12_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_12_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_11_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_11_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_10_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_10_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_9_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_9_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_8_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_8_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_7_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_7_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_6_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_6_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_5_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_5_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_4_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_4_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_3_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_3_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_2_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_2_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_1_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_1_foldedHist),
        .io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_0_foldedHist(io_toFtq_meta_bits_resolveMeta_phr_predFoldedHist_hist_0_foldedHist),
        .io_toFtq_meta_bits_commitMeta_ras_ssp(io_toFtq_meta_bits_commitMeta_ras_ssp),
        .io_toFtq_meta_bits_commitMeta_ras_tosw_flag(io_toFtq_meta_bits_commitMeta_ras_tosw_flag),
        .io_toFtq_meta_bits_commitMeta_ras_tosw_value(io_toFtq_meta_bits_commitMeta_ras_tosw_value),
        .io_toFtq_s3FtqPtr_flag(io_toFtq_s3FtqPtr_flag),
        .io_toFtq_s3FtqPtr_value(io_toFtq_s3FtqPtr_value),
        .io_toFtq_perfMeta_s1Prediction_cfiPosition(io_toFtq_perfMeta_s1Prediction_cfiPosition),
        .io_toFtq_perfMeta_s1Prediction_target_addr(io_toFtq_perfMeta_s1Prediction_target_addr),
        .io_toFtq_perfMeta_s1Prediction_attribute_branchType(io_toFtq_perfMeta_s1Prediction_attribute_branchType),
        .io_toFtq_perfMeta_s1Prediction_attribute_rasAction(io_toFtq_perfMeta_s1Prediction_attribute_rasAction),
        .io_toFtq_perfMeta_s1Prediction_taken(io_toFtq_perfMeta_s1Prediction_taken),
        .io_toFtq_perfMeta_s3Prediction_cfiPosition(io_toFtq_perfMeta_s3Prediction_cfiPosition),
        .io_toFtq_perfMeta_s3Prediction_target_addr(io_toFtq_perfMeta_s3Prediction_target_addr),
        .io_toFtq_perfMeta_s3Prediction_attribute_branchType(io_toFtq_perfMeta_s3Prediction_attribute_branchType),
        .io_toFtq_perfMeta_s3Prediction_attribute_rasAction(io_toFtq_perfMeta_s3Prediction_attribute_rasAction),
        .io_toFtq_perfMeta_s3Prediction_taken(io_toFtq_perfMeta_s3Prediction_taken),
        .io_toFtq_perfMeta_mbtbMeta_entries_0_0_rawHit(io_toFtq_perfMeta_mbtbMeta_entries_0_0_rawHit),
        .io_toFtq_perfMeta_mbtbMeta_entries_0_0_position(io_toFtq_perfMeta_mbtbMeta_entries_0_0_position),
        .io_toFtq_perfMeta_mbtbMeta_entries_0_1_rawHit(io_toFtq_perfMeta_mbtbMeta_entries_0_1_rawHit),
        .io_toFtq_perfMeta_mbtbMeta_entries_0_1_position(io_toFtq_perfMeta_mbtbMeta_entries_0_1_position),
        .io_toFtq_perfMeta_mbtbMeta_entries_0_2_rawHit(io_toFtq_perfMeta_mbtbMeta_entries_0_2_rawHit),
        .io_toFtq_perfMeta_mbtbMeta_entries_0_2_position(io_toFtq_perfMeta_mbtbMeta_entries_0_2_position),
        .io_toFtq_perfMeta_mbtbMeta_entries_0_3_rawHit(io_toFtq_perfMeta_mbtbMeta_entries_0_3_rawHit),
        .io_toFtq_perfMeta_mbtbMeta_entries_0_3_position(io_toFtq_perfMeta_mbtbMeta_entries_0_3_position),
        .io_toFtq_perfMeta_mbtbMeta_entries_1_0_rawHit(io_toFtq_perfMeta_mbtbMeta_entries_1_0_rawHit),
        .io_toFtq_perfMeta_mbtbMeta_entries_1_0_position(io_toFtq_perfMeta_mbtbMeta_entries_1_0_position),
        .io_toFtq_perfMeta_mbtbMeta_entries_1_1_rawHit(io_toFtq_perfMeta_mbtbMeta_entries_1_1_rawHit),
        .io_toFtq_perfMeta_mbtbMeta_entries_1_1_position(io_toFtq_perfMeta_mbtbMeta_entries_1_1_position),
        .io_toFtq_perfMeta_mbtbMeta_entries_1_2_rawHit(io_toFtq_perfMeta_mbtbMeta_entries_1_2_rawHit),
        .io_toFtq_perfMeta_mbtbMeta_entries_1_2_position(io_toFtq_perfMeta_mbtbMeta_entries_1_2_position),
        .io_toFtq_perfMeta_mbtbMeta_entries_1_3_rawHit(io_toFtq_perfMeta_mbtbMeta_entries_1_3_rawHit),
        .io_toFtq_perfMeta_mbtbMeta_entries_1_3_position(io_toFtq_perfMeta_mbtbMeta_entries_1_3_position),
        .io_toFtq_perfMeta_bpSource_s1Source(io_toFtq_perfMeta_bpSource_s1Source),
        .io_toFtq_perfMeta_bpSource_s3Source(io_toFtq_perfMeta_bpSource_s3Source),
        .io_toFtq_perfMeta_bpSource_s3Override(io_toFtq_perfMeta_bpSource_s3Override),
        .boreChildrenBd_bore_array(boreChildrenBd_bore_array),
        .boreChildrenBd_bore_all(boreChildrenBd_bore_all),
        .boreChildrenBd_bore_req(boreChildrenBd_bore_req),
        .boreChildrenBd_bore_ack(boreChildrenBd_bore_ack),
        .boreChildrenBd_bore_writeen(boreChildrenBd_bore_writeen),
        .boreChildrenBd_bore_be(boreChildrenBd_bore_be),
        .boreChildrenBd_bore_addr(boreChildrenBd_bore_addr),
        .boreChildrenBd_bore_indata(boreChildrenBd_bore_indata),
        .boreChildrenBd_bore_readen(boreChildrenBd_bore_readen),
        .boreChildrenBd_bore_addr_rd(boreChildrenBd_bore_addr_rd),
        .boreChildrenBd_bore_outdata(boreChildrenBd_bore_outdata),
        .boreChildrenBd_bore_1_array(boreChildrenBd_bore_1_array),
        .boreChildrenBd_bore_1_all(boreChildrenBd_bore_1_all),
        .boreChildrenBd_bore_1_req(boreChildrenBd_bore_1_req),
        .boreChildrenBd_bore_1_ack(boreChildrenBd_bore_1_ack),
        .boreChildrenBd_bore_1_writeen(boreChildrenBd_bore_1_writeen),
        .boreChildrenBd_bore_1_be(boreChildrenBd_bore_1_be),
        .boreChildrenBd_bore_1_addr(boreChildrenBd_bore_1_addr),
        .boreChildrenBd_bore_1_indata(boreChildrenBd_bore_1_indata),
        .boreChildrenBd_bore_1_readen(boreChildrenBd_bore_1_readen),
        .boreChildrenBd_bore_1_addr_rd(boreChildrenBd_bore_1_addr_rd),
        .boreChildrenBd_bore_1_outdata(boreChildrenBd_bore_1_outdata),
        .boreChildrenBd_bore_2_array(boreChildrenBd_bore_2_array),
        .boreChildrenBd_bore_2_all(boreChildrenBd_bore_2_all),
        .boreChildrenBd_bore_2_req(boreChildrenBd_bore_2_req),
        .boreChildrenBd_bore_2_ack(boreChildrenBd_bore_2_ack),
        .boreChildrenBd_bore_2_writeen(boreChildrenBd_bore_2_writeen),
        .boreChildrenBd_bore_2_be(boreChildrenBd_bore_2_be),
        .boreChildrenBd_bore_2_addr(boreChildrenBd_bore_2_addr),
        .boreChildrenBd_bore_2_indata(boreChildrenBd_bore_2_indata),
        .boreChildrenBd_bore_2_readen(boreChildrenBd_bore_2_readen),
        .boreChildrenBd_bore_2_addr_rd(boreChildrenBd_bore_2_addr_rd),
        .boreChildrenBd_bore_2_outdata(boreChildrenBd_bore_2_outdata),
        .boreChildrenBd_bore_3_array(boreChildrenBd_bore_3_array),
        .boreChildrenBd_bore_3_all(boreChildrenBd_bore_3_all),
        .boreChildrenBd_bore_3_req(boreChildrenBd_bore_3_req),
        .boreChildrenBd_bore_3_ack(boreChildrenBd_bore_3_ack),
        .boreChildrenBd_bore_3_writeen(boreChildrenBd_bore_3_writeen),
        .boreChildrenBd_bore_3_be(boreChildrenBd_bore_3_be),
        .boreChildrenBd_bore_3_addr(boreChildrenBd_bore_3_addr),
        .boreChildrenBd_bore_3_indata(boreChildrenBd_bore_3_indata),
        .boreChildrenBd_bore_3_readen(boreChildrenBd_bore_3_readen),
        .boreChildrenBd_bore_3_addr_rd(boreChildrenBd_bore_3_addr_rd),
        .boreChildrenBd_bore_3_outdata(boreChildrenBd_bore_3_outdata),
        .boreChildrenBd_bore_4_array(boreChildrenBd_bore_4_array),
        .boreChildrenBd_bore_4_all(boreChildrenBd_bore_4_all),
        .boreChildrenBd_bore_4_req(boreChildrenBd_bore_4_req),
        .boreChildrenBd_bore_4_ack(boreChildrenBd_bore_4_ack),
        .boreChildrenBd_bore_4_writeen(boreChildrenBd_bore_4_writeen),
        .boreChildrenBd_bore_4_be(boreChildrenBd_bore_4_be),
        .boreChildrenBd_bore_4_addr(boreChildrenBd_bore_4_addr),
        .boreChildrenBd_bore_4_indata(boreChildrenBd_bore_4_indata),
        .boreChildrenBd_bore_4_readen(boreChildrenBd_bore_4_readen),
        .boreChildrenBd_bore_4_addr_rd(boreChildrenBd_bore_4_addr_rd),
        .boreChildrenBd_bore_4_outdata(boreChildrenBd_bore_4_outdata),
        .boreChildrenBd_bore_5_addr(boreChildrenBd_bore_5_addr),
        .boreChildrenBd_bore_5_addr_rd(boreChildrenBd_bore_5_addr_rd),
        .boreChildrenBd_bore_5_wdata(boreChildrenBd_bore_5_wdata),
        .boreChildrenBd_bore_5_wmask(boreChildrenBd_bore_5_wmask),
        .boreChildrenBd_bore_5_re(boreChildrenBd_bore_5_re),
        .boreChildrenBd_bore_5_we(boreChildrenBd_bore_5_we),
        .boreChildrenBd_bore_5_rdata(boreChildrenBd_bore_5_rdata),
        .boreChildrenBd_bore_5_ack(boreChildrenBd_bore_5_ack),
        .boreChildrenBd_bore_5_selectedOH(boreChildrenBd_bore_5_selectedOH),
        .boreChildrenBd_bore_5_array(boreChildrenBd_bore_5_array),
        .boreChildrenBd_bore_6_addr(boreChildrenBd_bore_6_addr),
        .boreChildrenBd_bore_6_addr_rd(boreChildrenBd_bore_6_addr_rd),
        .boreChildrenBd_bore_6_wdata(boreChildrenBd_bore_6_wdata),
        .boreChildrenBd_bore_6_wmask(boreChildrenBd_bore_6_wmask),
        .boreChildrenBd_bore_6_re(boreChildrenBd_bore_6_re),
        .boreChildrenBd_bore_6_we(boreChildrenBd_bore_6_we),
        .boreChildrenBd_bore_6_rdata(boreChildrenBd_bore_6_rdata),
        .boreChildrenBd_bore_6_ack(boreChildrenBd_bore_6_ack),
        .boreChildrenBd_bore_6_selectedOH(boreChildrenBd_bore_6_selectedOH),
        .boreChildrenBd_bore_6_array(boreChildrenBd_bore_6_array),
        .boreChildrenBd_bore_7_addr(boreChildrenBd_bore_7_addr),
        .boreChildrenBd_bore_7_addr_rd(boreChildrenBd_bore_7_addr_rd),
        .boreChildrenBd_bore_7_wdata(boreChildrenBd_bore_7_wdata),
        .boreChildrenBd_bore_7_wmask(boreChildrenBd_bore_7_wmask),
        .boreChildrenBd_bore_7_re(boreChildrenBd_bore_7_re),
        .boreChildrenBd_bore_7_we(boreChildrenBd_bore_7_we),
        .boreChildrenBd_bore_7_rdata(boreChildrenBd_bore_7_rdata),
        .boreChildrenBd_bore_7_ack(boreChildrenBd_bore_7_ack),
        .boreChildrenBd_bore_7_selectedOH(boreChildrenBd_bore_7_selectedOH),
        .boreChildrenBd_bore_7_array(boreChildrenBd_bore_7_array),
        .boreChildrenBd_bore_8_addr(boreChildrenBd_bore_8_addr),
        .boreChildrenBd_bore_8_addr_rd(boreChildrenBd_bore_8_addr_rd),
        .boreChildrenBd_bore_8_wdata(boreChildrenBd_bore_8_wdata),
        .boreChildrenBd_bore_8_wmask(boreChildrenBd_bore_8_wmask),
        .boreChildrenBd_bore_8_re(boreChildrenBd_bore_8_re),
        .boreChildrenBd_bore_8_we(boreChildrenBd_bore_8_we),
        .boreChildrenBd_bore_8_rdata(boreChildrenBd_bore_8_rdata),
        .boreChildrenBd_bore_8_ack(boreChildrenBd_bore_8_ack),
        .boreChildrenBd_bore_8_selectedOH(boreChildrenBd_bore_8_selectedOH),
        .boreChildrenBd_bore_8_array(boreChildrenBd_bore_8_array),
        .boreChildrenBd_bore_9_addr(boreChildrenBd_bore_9_addr),
        .boreChildrenBd_bore_9_addr_rd(boreChildrenBd_bore_9_addr_rd),
        .boreChildrenBd_bore_9_wdata(boreChildrenBd_bore_9_wdata),
        .boreChildrenBd_bore_9_wmask(boreChildrenBd_bore_9_wmask),
        .boreChildrenBd_bore_9_re(boreChildrenBd_bore_9_re),
        .boreChildrenBd_bore_9_we(boreChildrenBd_bore_9_we),
        .boreChildrenBd_bore_9_rdata(boreChildrenBd_bore_9_rdata),
        .boreChildrenBd_bore_9_ack(boreChildrenBd_bore_9_ack),
        .boreChildrenBd_bore_9_selectedOH(boreChildrenBd_bore_9_selectedOH),
        .boreChildrenBd_bore_9_array(boreChildrenBd_bore_9_array),
        .boreChildrenBd_bore_10_addr(boreChildrenBd_bore_10_addr),
        .boreChildrenBd_bore_10_addr_rd(boreChildrenBd_bore_10_addr_rd),
        .boreChildrenBd_bore_10_wdata(boreChildrenBd_bore_10_wdata),
        .boreChildrenBd_bore_10_wmask(boreChildrenBd_bore_10_wmask),
        .boreChildrenBd_bore_10_re(boreChildrenBd_bore_10_re),
        .boreChildrenBd_bore_10_we(boreChildrenBd_bore_10_we),
        .boreChildrenBd_bore_10_rdata(boreChildrenBd_bore_10_rdata),
        .boreChildrenBd_bore_10_ack(boreChildrenBd_bore_10_ack),
        .boreChildrenBd_bore_10_selectedOH(boreChildrenBd_bore_10_selectedOH),
        .boreChildrenBd_bore_10_array(boreChildrenBd_bore_10_array),
        .boreChildrenBd_bore_11_addr(boreChildrenBd_bore_11_addr),
        .boreChildrenBd_bore_11_addr_rd(boreChildrenBd_bore_11_addr_rd),
        .boreChildrenBd_bore_11_wdata(boreChildrenBd_bore_11_wdata),
        .boreChildrenBd_bore_11_wmask(boreChildrenBd_bore_11_wmask),
        .boreChildrenBd_bore_11_re(boreChildrenBd_bore_11_re),
        .boreChildrenBd_bore_11_we(boreChildrenBd_bore_11_we),
        .boreChildrenBd_bore_11_rdata(boreChildrenBd_bore_11_rdata),
        .boreChildrenBd_bore_11_ack(boreChildrenBd_bore_11_ack),
        .boreChildrenBd_bore_11_selectedOH(boreChildrenBd_bore_11_selectedOH),
        .boreChildrenBd_bore_11_array(boreChildrenBd_bore_11_array),
        .boreChildrenBd_bore_12_addr(boreChildrenBd_bore_12_addr),
        .boreChildrenBd_bore_12_addr_rd(boreChildrenBd_bore_12_addr_rd),
        .boreChildrenBd_bore_12_wdata(boreChildrenBd_bore_12_wdata),
        .boreChildrenBd_bore_12_wmask(boreChildrenBd_bore_12_wmask),
        .boreChildrenBd_bore_12_re(boreChildrenBd_bore_12_re),
        .boreChildrenBd_bore_12_we(boreChildrenBd_bore_12_we),
        .boreChildrenBd_bore_12_rdata(boreChildrenBd_bore_12_rdata),
        .boreChildrenBd_bore_12_ack(boreChildrenBd_bore_12_ack),
        .boreChildrenBd_bore_12_selectedOH(boreChildrenBd_bore_12_selectedOH),
        .boreChildrenBd_bore_12_array(boreChildrenBd_bore_12_array),
        .boreChildrenBd_bore_13_addr(boreChildrenBd_bore_13_addr),
        .boreChildrenBd_bore_13_addr_rd(boreChildrenBd_bore_13_addr_rd),
        .boreChildrenBd_bore_13_wdata(boreChildrenBd_bore_13_wdata),
        .boreChildrenBd_bore_13_wmask(boreChildrenBd_bore_13_wmask),
        .boreChildrenBd_bore_13_re(boreChildrenBd_bore_13_re),
        .boreChildrenBd_bore_13_we(boreChildrenBd_bore_13_we),
        .boreChildrenBd_bore_13_rdata(boreChildrenBd_bore_13_rdata),
        .boreChildrenBd_bore_13_ack(boreChildrenBd_bore_13_ack),
        .boreChildrenBd_bore_13_selectedOH(boreChildrenBd_bore_13_selectedOH),
        .boreChildrenBd_bore_13_array(boreChildrenBd_bore_13_array),
        .boreChildrenBd_bore_14_addr(boreChildrenBd_bore_14_addr),
        .boreChildrenBd_bore_14_addr_rd(boreChildrenBd_bore_14_addr_rd),
        .boreChildrenBd_bore_14_wdata(boreChildrenBd_bore_14_wdata),
        .boreChildrenBd_bore_14_wmask(boreChildrenBd_bore_14_wmask),
        .boreChildrenBd_bore_14_re(boreChildrenBd_bore_14_re),
        .boreChildrenBd_bore_14_we(boreChildrenBd_bore_14_we),
        .boreChildrenBd_bore_14_rdata(boreChildrenBd_bore_14_rdata),
        .boreChildrenBd_bore_14_ack(boreChildrenBd_bore_14_ack),
        .boreChildrenBd_bore_14_selectedOH(boreChildrenBd_bore_14_selectedOH),
        .boreChildrenBd_bore_14_array(boreChildrenBd_bore_14_array),
        .boreChildrenBd_bore_15_addr(boreChildrenBd_bore_15_addr),
        .boreChildrenBd_bore_15_addr_rd(boreChildrenBd_bore_15_addr_rd),
        .boreChildrenBd_bore_15_wdata(boreChildrenBd_bore_15_wdata),
        .boreChildrenBd_bore_15_wmask(boreChildrenBd_bore_15_wmask),
        .boreChildrenBd_bore_15_re(boreChildrenBd_bore_15_re),
        .boreChildrenBd_bore_15_we(boreChildrenBd_bore_15_we),
        .boreChildrenBd_bore_15_rdata(boreChildrenBd_bore_15_rdata),
        .boreChildrenBd_bore_15_ack(boreChildrenBd_bore_15_ack),
        .boreChildrenBd_bore_15_selectedOH(boreChildrenBd_bore_15_selectedOH),
        .boreChildrenBd_bore_15_array(boreChildrenBd_bore_15_array),
        .boreChildrenBd_bore_16_addr(boreChildrenBd_bore_16_addr),
        .boreChildrenBd_bore_16_addr_rd(boreChildrenBd_bore_16_addr_rd),
        .boreChildrenBd_bore_16_wdata(boreChildrenBd_bore_16_wdata),
        .boreChildrenBd_bore_16_wmask(boreChildrenBd_bore_16_wmask),
        .boreChildrenBd_bore_16_re(boreChildrenBd_bore_16_re),
        .boreChildrenBd_bore_16_we(boreChildrenBd_bore_16_we),
        .boreChildrenBd_bore_16_rdata(boreChildrenBd_bore_16_rdata),
        .boreChildrenBd_bore_16_ack(boreChildrenBd_bore_16_ack),
        .boreChildrenBd_bore_16_selectedOH(boreChildrenBd_bore_16_selectedOH),
        .boreChildrenBd_bore_16_array(boreChildrenBd_bore_16_array),
        .boreChildrenBd_bore_17_addr(boreChildrenBd_bore_17_addr),
        .boreChildrenBd_bore_17_addr_rd(boreChildrenBd_bore_17_addr_rd),
        .boreChildrenBd_bore_17_wdata(boreChildrenBd_bore_17_wdata),
        .boreChildrenBd_bore_17_wmask(boreChildrenBd_bore_17_wmask),
        .boreChildrenBd_bore_17_re(boreChildrenBd_bore_17_re),
        .boreChildrenBd_bore_17_we(boreChildrenBd_bore_17_we),
        .boreChildrenBd_bore_17_rdata(boreChildrenBd_bore_17_rdata),
        .boreChildrenBd_bore_17_ack(boreChildrenBd_bore_17_ack),
        .boreChildrenBd_bore_17_selectedOH(boreChildrenBd_bore_17_selectedOH),
        .boreChildrenBd_bore_17_array(boreChildrenBd_bore_17_array),
        .boreChildrenBd_bore_18_addr(boreChildrenBd_bore_18_addr),
        .boreChildrenBd_bore_18_addr_rd(boreChildrenBd_bore_18_addr_rd),
        .boreChildrenBd_bore_18_wdata(boreChildrenBd_bore_18_wdata),
        .boreChildrenBd_bore_18_wmask(boreChildrenBd_bore_18_wmask),
        .boreChildrenBd_bore_18_re(boreChildrenBd_bore_18_re),
        .boreChildrenBd_bore_18_we(boreChildrenBd_bore_18_we),
        .boreChildrenBd_bore_18_rdata(boreChildrenBd_bore_18_rdata),
        .boreChildrenBd_bore_18_ack(boreChildrenBd_bore_18_ack),
        .boreChildrenBd_bore_18_selectedOH(boreChildrenBd_bore_18_selectedOH),
        .boreChildrenBd_bore_18_array(boreChildrenBd_bore_18_array),
        .sigFromSrams_bore_ram_hold(sigFromSrams_bore_ram_hold),
        .sigFromSrams_bore_ram_bypass(sigFromSrams_bore_ram_bypass),
        .sigFromSrams_bore_ram_bp_clken(sigFromSrams_bore_ram_bp_clken),
        .sigFromSrams_bore_ram_aux_clk(sigFromSrams_bore_ram_aux_clk),
        .sigFromSrams_bore_ram_aux_ckbp(sigFromSrams_bore_ram_aux_ckbp),
        .sigFromSrams_bore_ram_mcp_hold(sigFromSrams_bore_ram_mcp_hold),
        .sigFromSrams_bore_cgen(sigFromSrams_bore_cgen),
        .sigFromSrams_bore_1_ram_hold(sigFromSrams_bore_1_ram_hold),
        .sigFromSrams_bore_1_ram_bypass(sigFromSrams_bore_1_ram_bypass),
        .sigFromSrams_bore_1_ram_bp_clken(sigFromSrams_bore_1_ram_bp_clken),
        .sigFromSrams_bore_1_ram_aux_clk(sigFromSrams_bore_1_ram_aux_clk),
        .sigFromSrams_bore_1_ram_aux_ckbp(sigFromSrams_bore_1_ram_aux_ckbp),
        .sigFromSrams_bore_1_ram_mcp_hold(sigFromSrams_bore_1_ram_mcp_hold),
        .sigFromSrams_bore_1_cgen(sigFromSrams_bore_1_cgen),
        .sigFromSrams_bore_2_ram_hold(sigFromSrams_bore_2_ram_hold),
        .sigFromSrams_bore_2_ram_bypass(sigFromSrams_bore_2_ram_bypass),
        .sigFromSrams_bore_2_ram_bp_clken(sigFromSrams_bore_2_ram_bp_clken),
        .sigFromSrams_bore_2_ram_aux_clk(sigFromSrams_bore_2_ram_aux_clk),
        .sigFromSrams_bore_2_ram_aux_ckbp(sigFromSrams_bore_2_ram_aux_ckbp),
        .sigFromSrams_bore_2_ram_mcp_hold(sigFromSrams_bore_2_ram_mcp_hold),
        .sigFromSrams_bore_2_cgen(sigFromSrams_bore_2_cgen),
        .sigFromSrams_bore_3_ram_hold(sigFromSrams_bore_3_ram_hold),
        .sigFromSrams_bore_3_ram_bypass(sigFromSrams_bore_3_ram_bypass),
        .sigFromSrams_bore_3_ram_bp_clken(sigFromSrams_bore_3_ram_bp_clken),
        .sigFromSrams_bore_3_ram_aux_clk(sigFromSrams_bore_3_ram_aux_clk),
        .sigFromSrams_bore_3_ram_aux_ckbp(sigFromSrams_bore_3_ram_aux_ckbp),
        .sigFromSrams_bore_3_ram_mcp_hold(sigFromSrams_bore_3_ram_mcp_hold),
        .sigFromSrams_bore_3_cgen(sigFromSrams_bore_3_cgen),
        .sigFromSrams_bore_4_ram_hold(sigFromSrams_bore_4_ram_hold),
        .sigFromSrams_bore_4_ram_bypass(sigFromSrams_bore_4_ram_bypass),
        .sigFromSrams_bore_4_ram_bp_clken(sigFromSrams_bore_4_ram_bp_clken),
        .sigFromSrams_bore_4_ram_aux_clk(sigFromSrams_bore_4_ram_aux_clk),
        .sigFromSrams_bore_4_ram_aux_ckbp(sigFromSrams_bore_4_ram_aux_ckbp),
        .sigFromSrams_bore_4_ram_mcp_hold(sigFromSrams_bore_4_ram_mcp_hold),
        .sigFromSrams_bore_4_cgen(sigFromSrams_bore_4_cgen),
        .sigFromSrams_bore_5_ram_hold(sigFromSrams_bore_5_ram_hold),
        .sigFromSrams_bore_5_ram_bypass(sigFromSrams_bore_5_ram_bypass),
        .sigFromSrams_bore_5_ram_bp_clken(sigFromSrams_bore_5_ram_bp_clken),
        .sigFromSrams_bore_5_ram_aux_clk(sigFromSrams_bore_5_ram_aux_clk),
        .sigFromSrams_bore_5_ram_aux_ckbp(sigFromSrams_bore_5_ram_aux_ckbp),
        .sigFromSrams_bore_5_ram_mcp_hold(sigFromSrams_bore_5_ram_mcp_hold),
        .sigFromSrams_bore_5_cgen(sigFromSrams_bore_5_cgen),
        .sigFromSrams_bore_6_ram_hold(sigFromSrams_bore_6_ram_hold),
        .sigFromSrams_bore_6_ram_bypass(sigFromSrams_bore_6_ram_bypass),
        .sigFromSrams_bore_6_ram_bp_clken(sigFromSrams_bore_6_ram_bp_clken),
        .sigFromSrams_bore_6_ram_aux_clk(sigFromSrams_bore_6_ram_aux_clk),
        .sigFromSrams_bore_6_ram_aux_ckbp(sigFromSrams_bore_6_ram_aux_ckbp),
        .sigFromSrams_bore_6_ram_mcp_hold(sigFromSrams_bore_6_ram_mcp_hold),
        .sigFromSrams_bore_6_cgen(sigFromSrams_bore_6_cgen),
        .sigFromSrams_bore_7_ram_hold(sigFromSrams_bore_7_ram_hold),
        .sigFromSrams_bore_7_ram_bypass(sigFromSrams_bore_7_ram_bypass),
        .sigFromSrams_bore_7_ram_bp_clken(sigFromSrams_bore_7_ram_bp_clken),
        .sigFromSrams_bore_7_ram_aux_clk(sigFromSrams_bore_7_ram_aux_clk),
        .sigFromSrams_bore_7_ram_aux_ckbp(sigFromSrams_bore_7_ram_aux_ckbp),
        .sigFromSrams_bore_7_ram_mcp_hold(sigFromSrams_bore_7_ram_mcp_hold),
        .sigFromSrams_bore_7_cgen(sigFromSrams_bore_7_cgen),
        .sigFromSrams_bore_8_ram_hold(sigFromSrams_bore_8_ram_hold),
        .sigFromSrams_bore_8_ram_bypass(sigFromSrams_bore_8_ram_bypass),
        .sigFromSrams_bore_8_ram_bp_clken(sigFromSrams_bore_8_ram_bp_clken),
        .sigFromSrams_bore_8_ram_aux_clk(sigFromSrams_bore_8_ram_aux_clk),
        .sigFromSrams_bore_8_ram_aux_ckbp(sigFromSrams_bore_8_ram_aux_ckbp),
        .sigFromSrams_bore_8_ram_mcp_hold(sigFromSrams_bore_8_ram_mcp_hold),
        .sigFromSrams_bore_8_cgen(sigFromSrams_bore_8_cgen),
        .sigFromSrams_bore_9_ram_hold(sigFromSrams_bore_9_ram_hold),
        .sigFromSrams_bore_9_ram_bypass(sigFromSrams_bore_9_ram_bypass),
        .sigFromSrams_bore_9_ram_bp_clken(sigFromSrams_bore_9_ram_bp_clken),
        .sigFromSrams_bore_9_ram_aux_clk(sigFromSrams_bore_9_ram_aux_clk),
        .sigFromSrams_bore_9_ram_aux_ckbp(sigFromSrams_bore_9_ram_aux_ckbp),
        .sigFromSrams_bore_9_ram_mcp_hold(sigFromSrams_bore_9_ram_mcp_hold),
        .sigFromSrams_bore_9_cgen(sigFromSrams_bore_9_cgen),
        .sigFromSrams_bore_10_ram_hold(sigFromSrams_bore_10_ram_hold),
        .sigFromSrams_bore_10_ram_bypass(sigFromSrams_bore_10_ram_bypass),
        .sigFromSrams_bore_10_ram_bp_clken(sigFromSrams_bore_10_ram_bp_clken),
        .sigFromSrams_bore_10_ram_aux_clk(sigFromSrams_bore_10_ram_aux_clk),
        .sigFromSrams_bore_10_ram_aux_ckbp(sigFromSrams_bore_10_ram_aux_ckbp),
        .sigFromSrams_bore_10_ram_mcp_hold(sigFromSrams_bore_10_ram_mcp_hold),
        .sigFromSrams_bore_10_cgen(sigFromSrams_bore_10_cgen),
        .sigFromSrams_bore_11_ram_hold(sigFromSrams_bore_11_ram_hold),
        .sigFromSrams_bore_11_ram_bypass(sigFromSrams_bore_11_ram_bypass),
        .sigFromSrams_bore_11_ram_bp_clken(sigFromSrams_bore_11_ram_bp_clken),
        .sigFromSrams_bore_11_ram_aux_clk(sigFromSrams_bore_11_ram_aux_clk),
        .sigFromSrams_bore_11_ram_aux_ckbp(sigFromSrams_bore_11_ram_aux_ckbp),
        .sigFromSrams_bore_11_ram_mcp_hold(sigFromSrams_bore_11_ram_mcp_hold),
        .sigFromSrams_bore_11_cgen(sigFromSrams_bore_11_cgen),
        .sigFromSrams_bore_12_ram_hold(sigFromSrams_bore_12_ram_hold),
        .sigFromSrams_bore_12_ram_bypass(sigFromSrams_bore_12_ram_bypass),
        .sigFromSrams_bore_12_ram_bp_clken(sigFromSrams_bore_12_ram_bp_clken),
        .sigFromSrams_bore_12_ram_aux_clk(sigFromSrams_bore_12_ram_aux_clk),
        .sigFromSrams_bore_12_ram_aux_ckbp(sigFromSrams_bore_12_ram_aux_ckbp),
        .sigFromSrams_bore_12_ram_mcp_hold(sigFromSrams_bore_12_ram_mcp_hold),
        .sigFromSrams_bore_12_cgen(sigFromSrams_bore_12_cgen),
        .sigFromSrams_bore_13_ram_hold(sigFromSrams_bore_13_ram_hold),
        .sigFromSrams_bore_13_ram_bypass(sigFromSrams_bore_13_ram_bypass),
        .sigFromSrams_bore_13_ram_bp_clken(sigFromSrams_bore_13_ram_bp_clken),
        .sigFromSrams_bore_13_ram_aux_clk(sigFromSrams_bore_13_ram_aux_clk),
        .sigFromSrams_bore_13_ram_aux_ckbp(sigFromSrams_bore_13_ram_aux_ckbp),
        .sigFromSrams_bore_13_ram_mcp_hold(sigFromSrams_bore_13_ram_mcp_hold),
        .sigFromSrams_bore_13_cgen(sigFromSrams_bore_13_cgen),
        .sigFromSrams_bore_14_ram_hold(sigFromSrams_bore_14_ram_hold),
        .sigFromSrams_bore_14_ram_bypass(sigFromSrams_bore_14_ram_bypass),
        .sigFromSrams_bore_14_ram_bp_clken(sigFromSrams_bore_14_ram_bp_clken),
        .sigFromSrams_bore_14_ram_aux_clk(sigFromSrams_bore_14_ram_aux_clk),
        .sigFromSrams_bore_14_ram_aux_ckbp(sigFromSrams_bore_14_ram_aux_ckbp),
        .sigFromSrams_bore_14_ram_mcp_hold(sigFromSrams_bore_14_ram_mcp_hold),
        .sigFromSrams_bore_14_cgen(sigFromSrams_bore_14_cgen),
        .sigFromSrams_bore_15_ram_hold(sigFromSrams_bore_15_ram_hold),
        .sigFromSrams_bore_15_ram_bypass(sigFromSrams_bore_15_ram_bypass),
        .sigFromSrams_bore_15_ram_bp_clken(sigFromSrams_bore_15_ram_bp_clken),
        .sigFromSrams_bore_15_ram_aux_clk(sigFromSrams_bore_15_ram_aux_clk),
        .sigFromSrams_bore_15_ram_aux_ckbp(sigFromSrams_bore_15_ram_aux_ckbp),
        .sigFromSrams_bore_15_ram_mcp_hold(sigFromSrams_bore_15_ram_mcp_hold),
        .sigFromSrams_bore_15_cgen(sigFromSrams_bore_15_cgen),
        .sigFromSrams_bore_16_ram_hold(sigFromSrams_bore_16_ram_hold),
        .sigFromSrams_bore_16_ram_bypass(sigFromSrams_bore_16_ram_bypass),
        .sigFromSrams_bore_16_ram_bp_clken(sigFromSrams_bore_16_ram_bp_clken),
        .sigFromSrams_bore_16_ram_aux_clk(sigFromSrams_bore_16_ram_aux_clk),
        .sigFromSrams_bore_16_ram_aux_ckbp(sigFromSrams_bore_16_ram_aux_ckbp),
        .sigFromSrams_bore_16_ram_mcp_hold(sigFromSrams_bore_16_ram_mcp_hold),
        .sigFromSrams_bore_16_cgen(sigFromSrams_bore_16_cgen),
        .sigFromSrams_bore_17_ram_hold(sigFromSrams_bore_17_ram_hold),
        .sigFromSrams_bore_17_ram_bypass(sigFromSrams_bore_17_ram_bypass),
        .sigFromSrams_bore_17_ram_bp_clken(sigFromSrams_bore_17_ram_bp_clken),
        .sigFromSrams_bore_17_ram_aux_clk(sigFromSrams_bore_17_ram_aux_clk),
        .sigFromSrams_bore_17_ram_aux_ckbp(sigFromSrams_bore_17_ram_aux_ckbp),
        .sigFromSrams_bore_17_ram_mcp_hold(sigFromSrams_bore_17_ram_mcp_hold),
        .sigFromSrams_bore_17_cgen(sigFromSrams_bore_17_cgen),
        .sigFromSrams_bore_18_ram_hold(sigFromSrams_bore_18_ram_hold),
        .sigFromSrams_bore_18_ram_bypass(sigFromSrams_bore_18_ram_bypass),
        .sigFromSrams_bore_18_ram_bp_clken(sigFromSrams_bore_18_ram_bp_clken),
        .sigFromSrams_bore_18_ram_aux_clk(sigFromSrams_bore_18_ram_aux_clk),
        .sigFromSrams_bore_18_ram_aux_ckbp(sigFromSrams_bore_18_ram_aux_ckbp),
        .sigFromSrams_bore_18_ram_mcp_hold(sigFromSrams_bore_18_ram_mcp_hold),
        .sigFromSrams_bore_18_cgen(sigFromSrams_bore_18_cgen),
        .sigFromSrams_bore_19_ram_hold(sigFromSrams_bore_19_ram_hold),
        .sigFromSrams_bore_19_ram_bypass(sigFromSrams_bore_19_ram_bypass),
        .sigFromSrams_bore_19_ram_bp_clken(sigFromSrams_bore_19_ram_bp_clken),
        .sigFromSrams_bore_19_ram_aux_clk(sigFromSrams_bore_19_ram_aux_clk),
        .sigFromSrams_bore_19_ram_aux_ckbp(sigFromSrams_bore_19_ram_aux_ckbp),
        .sigFromSrams_bore_19_ram_mcp_hold(sigFromSrams_bore_19_ram_mcp_hold),
        .sigFromSrams_bore_19_cgen(sigFromSrams_bore_19_cgen),
        .sigFromSrams_bore_20_ram_hold(sigFromSrams_bore_20_ram_hold),
        .sigFromSrams_bore_20_ram_bypass(sigFromSrams_bore_20_ram_bypass),
        .sigFromSrams_bore_20_ram_bp_clken(sigFromSrams_bore_20_ram_bp_clken),
        .sigFromSrams_bore_20_ram_aux_clk(sigFromSrams_bore_20_ram_aux_clk),
        .sigFromSrams_bore_20_ram_aux_ckbp(sigFromSrams_bore_20_ram_aux_ckbp),
        .sigFromSrams_bore_20_ram_mcp_hold(sigFromSrams_bore_20_ram_mcp_hold),
        .sigFromSrams_bore_20_cgen(sigFromSrams_bore_20_cgen),
        .sigFromSrams_bore_21_ram_hold(sigFromSrams_bore_21_ram_hold),
        .sigFromSrams_bore_21_ram_bypass(sigFromSrams_bore_21_ram_bypass),
        .sigFromSrams_bore_21_ram_bp_clken(sigFromSrams_bore_21_ram_bp_clken),
        .sigFromSrams_bore_21_ram_aux_clk(sigFromSrams_bore_21_ram_aux_clk),
        .sigFromSrams_bore_21_ram_aux_ckbp(sigFromSrams_bore_21_ram_aux_ckbp),
        .sigFromSrams_bore_21_ram_mcp_hold(sigFromSrams_bore_21_ram_mcp_hold),
        .sigFromSrams_bore_21_cgen(sigFromSrams_bore_21_cgen),
        .sigFromSrams_bore_22_ram_hold(sigFromSrams_bore_22_ram_hold),
        .sigFromSrams_bore_22_ram_bypass(sigFromSrams_bore_22_ram_bypass),
        .sigFromSrams_bore_22_ram_bp_clken(sigFromSrams_bore_22_ram_bp_clken),
        .sigFromSrams_bore_22_ram_aux_clk(sigFromSrams_bore_22_ram_aux_clk),
        .sigFromSrams_bore_22_ram_aux_ckbp(sigFromSrams_bore_22_ram_aux_ckbp),
        .sigFromSrams_bore_22_ram_mcp_hold(sigFromSrams_bore_22_ram_mcp_hold),
        .sigFromSrams_bore_22_cgen(sigFromSrams_bore_22_cgen),
        .sigFromSrams_bore_23_ram_hold(sigFromSrams_bore_23_ram_hold),
        .sigFromSrams_bore_23_ram_bypass(sigFromSrams_bore_23_ram_bypass),
        .sigFromSrams_bore_23_ram_bp_clken(sigFromSrams_bore_23_ram_bp_clken),
        .sigFromSrams_bore_23_ram_aux_clk(sigFromSrams_bore_23_ram_aux_clk),
        .sigFromSrams_bore_23_ram_aux_ckbp(sigFromSrams_bore_23_ram_aux_ckbp),
        .sigFromSrams_bore_23_ram_mcp_hold(sigFromSrams_bore_23_ram_mcp_hold),
        .sigFromSrams_bore_23_cgen(sigFromSrams_bore_23_cgen),
        .sigFromSrams_bore_24_ram_hold(sigFromSrams_bore_24_ram_hold),
        .sigFromSrams_bore_24_ram_bypass(sigFromSrams_bore_24_ram_bypass),
        .sigFromSrams_bore_24_ram_bp_clken(sigFromSrams_bore_24_ram_bp_clken),
        .sigFromSrams_bore_24_ram_aux_clk(sigFromSrams_bore_24_ram_aux_clk),
        .sigFromSrams_bore_24_ram_aux_ckbp(sigFromSrams_bore_24_ram_aux_ckbp),
        .sigFromSrams_bore_24_ram_mcp_hold(sigFromSrams_bore_24_ram_mcp_hold),
        .sigFromSrams_bore_24_cgen(sigFromSrams_bore_24_cgen),
        .sigFromSrams_bore_25_ram_hold(sigFromSrams_bore_25_ram_hold),
        .sigFromSrams_bore_25_ram_bypass(sigFromSrams_bore_25_ram_bypass),
        .sigFromSrams_bore_25_ram_bp_clken(sigFromSrams_bore_25_ram_bp_clken),
        .sigFromSrams_bore_25_ram_aux_clk(sigFromSrams_bore_25_ram_aux_clk),
        .sigFromSrams_bore_25_ram_aux_ckbp(sigFromSrams_bore_25_ram_aux_ckbp),
        .sigFromSrams_bore_25_ram_mcp_hold(sigFromSrams_bore_25_ram_mcp_hold),
        .sigFromSrams_bore_25_cgen(sigFromSrams_bore_25_cgen),
        .sigFromSrams_bore_26_ram_hold(sigFromSrams_bore_26_ram_hold),
        .sigFromSrams_bore_26_ram_bypass(sigFromSrams_bore_26_ram_bypass),
        .sigFromSrams_bore_26_ram_bp_clken(sigFromSrams_bore_26_ram_bp_clken),
        .sigFromSrams_bore_26_ram_aux_clk(sigFromSrams_bore_26_ram_aux_clk),
        .sigFromSrams_bore_26_ram_aux_ckbp(sigFromSrams_bore_26_ram_aux_ckbp),
        .sigFromSrams_bore_26_ram_mcp_hold(sigFromSrams_bore_26_ram_mcp_hold),
        .sigFromSrams_bore_26_cgen(sigFromSrams_bore_26_cgen),
        .sigFromSrams_bore_27_ram_hold(sigFromSrams_bore_27_ram_hold),
        .sigFromSrams_bore_27_ram_bypass(sigFromSrams_bore_27_ram_bypass),
        .sigFromSrams_bore_27_ram_bp_clken(sigFromSrams_bore_27_ram_bp_clken),
        .sigFromSrams_bore_27_ram_aux_clk(sigFromSrams_bore_27_ram_aux_clk),
        .sigFromSrams_bore_27_ram_aux_ckbp(sigFromSrams_bore_27_ram_aux_ckbp),
        .sigFromSrams_bore_27_ram_mcp_hold(sigFromSrams_bore_27_ram_mcp_hold),
        .sigFromSrams_bore_27_cgen(sigFromSrams_bore_27_cgen),
        .sigFromSrams_bore_28_ram_hold(sigFromSrams_bore_28_ram_hold),
        .sigFromSrams_bore_28_ram_bypass(sigFromSrams_bore_28_ram_bypass),
        .sigFromSrams_bore_28_ram_bp_clken(sigFromSrams_bore_28_ram_bp_clken),
        .sigFromSrams_bore_28_ram_aux_clk(sigFromSrams_bore_28_ram_aux_clk),
        .sigFromSrams_bore_28_ram_aux_ckbp(sigFromSrams_bore_28_ram_aux_ckbp),
        .sigFromSrams_bore_28_ram_mcp_hold(sigFromSrams_bore_28_ram_mcp_hold),
        .sigFromSrams_bore_28_cgen(sigFromSrams_bore_28_cgen),
        .sigFromSrams_bore_29_ram_hold(sigFromSrams_bore_29_ram_hold),
        .sigFromSrams_bore_29_ram_bypass(sigFromSrams_bore_29_ram_bypass),
        .sigFromSrams_bore_29_ram_bp_clken(sigFromSrams_bore_29_ram_bp_clken),
        .sigFromSrams_bore_29_ram_aux_clk(sigFromSrams_bore_29_ram_aux_clk),
        .sigFromSrams_bore_29_ram_aux_ckbp(sigFromSrams_bore_29_ram_aux_ckbp),
        .sigFromSrams_bore_29_ram_mcp_hold(sigFromSrams_bore_29_ram_mcp_hold),
        .sigFromSrams_bore_29_cgen(sigFromSrams_bore_29_cgen),
        .sigFromSrams_bore_30_ram_hold(sigFromSrams_bore_30_ram_hold),
        .sigFromSrams_bore_30_ram_bypass(sigFromSrams_bore_30_ram_bypass),
        .sigFromSrams_bore_30_ram_bp_clken(sigFromSrams_bore_30_ram_bp_clken),
        .sigFromSrams_bore_30_ram_aux_clk(sigFromSrams_bore_30_ram_aux_clk),
        .sigFromSrams_bore_30_ram_aux_ckbp(sigFromSrams_bore_30_ram_aux_ckbp),
        .sigFromSrams_bore_30_ram_mcp_hold(sigFromSrams_bore_30_ram_mcp_hold),
        .sigFromSrams_bore_30_cgen(sigFromSrams_bore_30_cgen),
        .sigFromSrams_bore_31_ram_hold(sigFromSrams_bore_31_ram_hold),
        .sigFromSrams_bore_31_ram_bypass(sigFromSrams_bore_31_ram_bypass),
        .sigFromSrams_bore_31_ram_bp_clken(sigFromSrams_bore_31_ram_bp_clken),
        .sigFromSrams_bore_31_ram_aux_clk(sigFromSrams_bore_31_ram_aux_clk),
        .sigFromSrams_bore_31_ram_aux_ckbp(sigFromSrams_bore_31_ram_aux_ckbp),
        .sigFromSrams_bore_31_ram_mcp_hold(sigFromSrams_bore_31_ram_mcp_hold),
        .sigFromSrams_bore_31_cgen(sigFromSrams_bore_31_cgen),
        .sigFromSrams_bore_32_ram_hold(sigFromSrams_bore_32_ram_hold),
        .sigFromSrams_bore_32_ram_bypass(sigFromSrams_bore_32_ram_bypass),
        .sigFromSrams_bore_32_ram_bp_clken(sigFromSrams_bore_32_ram_bp_clken),
        .sigFromSrams_bore_32_ram_aux_clk(sigFromSrams_bore_32_ram_aux_clk),
        .sigFromSrams_bore_32_ram_aux_ckbp(sigFromSrams_bore_32_ram_aux_ckbp),
        .sigFromSrams_bore_32_ram_mcp_hold(sigFromSrams_bore_32_ram_mcp_hold),
        .sigFromSrams_bore_32_cgen(sigFromSrams_bore_32_cgen),
        .sigFromSrams_bore_33_ram_hold(sigFromSrams_bore_33_ram_hold),
        .sigFromSrams_bore_33_ram_bypass(sigFromSrams_bore_33_ram_bypass),
        .sigFromSrams_bore_33_ram_bp_clken(sigFromSrams_bore_33_ram_bp_clken),
        .sigFromSrams_bore_33_ram_aux_clk(sigFromSrams_bore_33_ram_aux_clk),
        .sigFromSrams_bore_33_ram_aux_ckbp(sigFromSrams_bore_33_ram_aux_ckbp),
        .sigFromSrams_bore_33_ram_mcp_hold(sigFromSrams_bore_33_ram_mcp_hold),
        .sigFromSrams_bore_33_cgen(sigFromSrams_bore_33_cgen),
        .sigFromSrams_bore_34_ram_hold(sigFromSrams_bore_34_ram_hold),
        .sigFromSrams_bore_34_ram_bypass(sigFromSrams_bore_34_ram_bypass),
        .sigFromSrams_bore_34_ram_bp_clken(sigFromSrams_bore_34_ram_bp_clken),
        .sigFromSrams_bore_34_ram_aux_clk(sigFromSrams_bore_34_ram_aux_clk),
        .sigFromSrams_bore_34_ram_aux_ckbp(sigFromSrams_bore_34_ram_aux_ckbp),
        .sigFromSrams_bore_34_ram_mcp_hold(sigFromSrams_bore_34_ram_mcp_hold),
        .sigFromSrams_bore_34_cgen(sigFromSrams_bore_34_cgen),
        .sigFromSrams_bore_35_ram_hold(sigFromSrams_bore_35_ram_hold),
        .sigFromSrams_bore_35_ram_bypass(sigFromSrams_bore_35_ram_bypass),
        .sigFromSrams_bore_35_ram_bp_clken(sigFromSrams_bore_35_ram_bp_clken),
        .sigFromSrams_bore_35_ram_aux_clk(sigFromSrams_bore_35_ram_aux_clk),
        .sigFromSrams_bore_35_ram_aux_ckbp(sigFromSrams_bore_35_ram_aux_ckbp),
        .sigFromSrams_bore_35_ram_mcp_hold(sigFromSrams_bore_35_ram_mcp_hold),
        .sigFromSrams_bore_35_cgen(sigFromSrams_bore_35_cgen),
        .sigFromSrams_bore_36_ram_hold(sigFromSrams_bore_36_ram_hold),
        .sigFromSrams_bore_36_ram_bypass(sigFromSrams_bore_36_ram_bypass),
        .sigFromSrams_bore_36_ram_bp_clken(sigFromSrams_bore_36_ram_bp_clken),
        .sigFromSrams_bore_36_ram_aux_clk(sigFromSrams_bore_36_ram_aux_clk),
        .sigFromSrams_bore_36_ram_aux_ckbp(sigFromSrams_bore_36_ram_aux_ckbp),
        .sigFromSrams_bore_36_ram_mcp_hold(sigFromSrams_bore_36_ram_mcp_hold),
        .sigFromSrams_bore_36_cgen(sigFromSrams_bore_36_cgen),
        .sigFromSrams_bore_37_ram_hold(sigFromSrams_bore_37_ram_hold),
        .sigFromSrams_bore_37_ram_bypass(sigFromSrams_bore_37_ram_bypass),
        .sigFromSrams_bore_37_ram_bp_clken(sigFromSrams_bore_37_ram_bp_clken),
        .sigFromSrams_bore_37_ram_aux_clk(sigFromSrams_bore_37_ram_aux_clk),
        .sigFromSrams_bore_37_ram_aux_ckbp(sigFromSrams_bore_37_ram_aux_ckbp),
        .sigFromSrams_bore_37_ram_mcp_hold(sigFromSrams_bore_37_ram_mcp_hold),
        .sigFromSrams_bore_37_cgen(sigFromSrams_bore_37_cgen),
        .sigFromSrams_bore_38_ram_hold(sigFromSrams_bore_38_ram_hold),
        .sigFromSrams_bore_38_ram_bypass(sigFromSrams_bore_38_ram_bypass),
        .sigFromSrams_bore_38_ram_bp_clken(sigFromSrams_bore_38_ram_bp_clken),
        .sigFromSrams_bore_38_ram_aux_clk(sigFromSrams_bore_38_ram_aux_clk),
        .sigFromSrams_bore_38_ram_aux_ckbp(sigFromSrams_bore_38_ram_aux_ckbp),
        .sigFromSrams_bore_38_ram_mcp_hold(sigFromSrams_bore_38_ram_mcp_hold),
        .sigFromSrams_bore_38_cgen(sigFromSrams_bore_38_cgen),
        .sigFromSrams_bore_39_ram_hold(sigFromSrams_bore_39_ram_hold),
        .sigFromSrams_bore_39_ram_bypass(sigFromSrams_bore_39_ram_bypass),
        .sigFromSrams_bore_39_ram_bp_clken(sigFromSrams_bore_39_ram_bp_clken),
        .sigFromSrams_bore_39_ram_aux_clk(sigFromSrams_bore_39_ram_aux_clk),
        .sigFromSrams_bore_39_ram_aux_ckbp(sigFromSrams_bore_39_ram_aux_ckbp),
        .sigFromSrams_bore_39_ram_mcp_hold(sigFromSrams_bore_39_ram_mcp_hold),
        .sigFromSrams_bore_39_cgen(sigFromSrams_bore_39_cgen),
        .sigFromSrams_bore_40_ram_hold(sigFromSrams_bore_40_ram_hold),
        .sigFromSrams_bore_40_ram_bypass(sigFromSrams_bore_40_ram_bypass),
        .sigFromSrams_bore_40_ram_bp_clken(sigFromSrams_bore_40_ram_bp_clken),
        .sigFromSrams_bore_40_ram_aux_clk(sigFromSrams_bore_40_ram_aux_clk),
        .sigFromSrams_bore_40_ram_aux_ckbp(sigFromSrams_bore_40_ram_aux_ckbp),
        .sigFromSrams_bore_40_ram_mcp_hold(sigFromSrams_bore_40_ram_mcp_hold),
        .sigFromSrams_bore_40_cgen(sigFromSrams_bore_40_cgen),
        .sigFromSrams_bore_41_ram_hold(sigFromSrams_bore_41_ram_hold),
        .sigFromSrams_bore_41_ram_bypass(sigFromSrams_bore_41_ram_bypass),
        .sigFromSrams_bore_41_ram_bp_clken(sigFromSrams_bore_41_ram_bp_clken),
        .sigFromSrams_bore_41_ram_aux_clk(sigFromSrams_bore_41_ram_aux_clk),
        .sigFromSrams_bore_41_ram_aux_ckbp(sigFromSrams_bore_41_ram_aux_ckbp),
        .sigFromSrams_bore_41_ram_mcp_hold(sigFromSrams_bore_41_ram_mcp_hold),
        .sigFromSrams_bore_41_cgen(sigFromSrams_bore_41_cgen),
        .sigFromSrams_bore_42_ram_hold(sigFromSrams_bore_42_ram_hold),
        .sigFromSrams_bore_42_ram_bypass(sigFromSrams_bore_42_ram_bypass),
        .sigFromSrams_bore_42_ram_bp_clken(sigFromSrams_bore_42_ram_bp_clken),
        .sigFromSrams_bore_42_ram_aux_clk(sigFromSrams_bore_42_ram_aux_clk),
        .sigFromSrams_bore_42_ram_aux_ckbp(sigFromSrams_bore_42_ram_aux_ckbp),
        .sigFromSrams_bore_42_ram_mcp_hold(sigFromSrams_bore_42_ram_mcp_hold),
        .sigFromSrams_bore_42_cgen(sigFromSrams_bore_42_cgen),
        .sigFromSrams_bore_43_ram_hold(sigFromSrams_bore_43_ram_hold),
        .sigFromSrams_bore_43_ram_bypass(sigFromSrams_bore_43_ram_bypass),
        .sigFromSrams_bore_43_ram_bp_clken(sigFromSrams_bore_43_ram_bp_clken),
        .sigFromSrams_bore_43_ram_aux_clk(sigFromSrams_bore_43_ram_aux_clk),
        .sigFromSrams_bore_43_ram_aux_ckbp(sigFromSrams_bore_43_ram_aux_ckbp),
        .sigFromSrams_bore_43_ram_mcp_hold(sigFromSrams_bore_43_ram_mcp_hold),
        .sigFromSrams_bore_43_cgen(sigFromSrams_bore_43_cgen),
        .sigFromSrams_bore_44_ram_hold(sigFromSrams_bore_44_ram_hold),
        .sigFromSrams_bore_44_ram_bypass(sigFromSrams_bore_44_ram_bypass),
        .sigFromSrams_bore_44_ram_bp_clken(sigFromSrams_bore_44_ram_bp_clken),
        .sigFromSrams_bore_44_ram_aux_clk(sigFromSrams_bore_44_ram_aux_clk),
        .sigFromSrams_bore_44_ram_aux_ckbp(sigFromSrams_bore_44_ram_aux_ckbp),
        .sigFromSrams_bore_44_ram_mcp_hold(sigFromSrams_bore_44_ram_mcp_hold),
        .sigFromSrams_bore_44_cgen(sigFromSrams_bore_44_cgen),
        .sigFromSrams_bore_45_ram_hold(sigFromSrams_bore_45_ram_hold),
        .sigFromSrams_bore_45_ram_bypass(sigFromSrams_bore_45_ram_bypass),
        .sigFromSrams_bore_45_ram_bp_clken(sigFromSrams_bore_45_ram_bp_clken),
        .sigFromSrams_bore_45_ram_aux_clk(sigFromSrams_bore_45_ram_aux_clk),
        .sigFromSrams_bore_45_ram_aux_ckbp(sigFromSrams_bore_45_ram_aux_ckbp),
        .sigFromSrams_bore_45_ram_mcp_hold(sigFromSrams_bore_45_ram_mcp_hold),
        .sigFromSrams_bore_45_cgen(sigFromSrams_bore_45_cgen),
        .sigFromSrams_bore_46_ram_hold(sigFromSrams_bore_46_ram_hold),
        .sigFromSrams_bore_46_ram_bypass(sigFromSrams_bore_46_ram_bypass),
        .sigFromSrams_bore_46_ram_bp_clken(sigFromSrams_bore_46_ram_bp_clken),
        .sigFromSrams_bore_46_ram_aux_clk(sigFromSrams_bore_46_ram_aux_clk),
        .sigFromSrams_bore_46_ram_aux_ckbp(sigFromSrams_bore_46_ram_aux_ckbp),
        .sigFromSrams_bore_46_ram_mcp_hold(sigFromSrams_bore_46_ram_mcp_hold),
        .sigFromSrams_bore_46_cgen(sigFromSrams_bore_46_cgen),
        .sigFromSrams_bore_47_ram_hold(sigFromSrams_bore_47_ram_hold),
        .sigFromSrams_bore_47_ram_bypass(sigFromSrams_bore_47_ram_bypass),
        .sigFromSrams_bore_47_ram_bp_clken(sigFromSrams_bore_47_ram_bp_clken),
        .sigFromSrams_bore_47_ram_aux_clk(sigFromSrams_bore_47_ram_aux_clk),
        .sigFromSrams_bore_47_ram_aux_ckbp(sigFromSrams_bore_47_ram_aux_ckbp),
        .sigFromSrams_bore_47_ram_mcp_hold(sigFromSrams_bore_47_ram_mcp_hold),
        .sigFromSrams_bore_47_cgen(sigFromSrams_bore_47_cgen),
        .sigFromSrams_bore_48_ram_hold(sigFromSrams_bore_48_ram_hold),
        .sigFromSrams_bore_48_ram_bypass(sigFromSrams_bore_48_ram_bypass),
        .sigFromSrams_bore_48_ram_bp_clken(sigFromSrams_bore_48_ram_bp_clken),
        .sigFromSrams_bore_48_ram_aux_clk(sigFromSrams_bore_48_ram_aux_clk),
        .sigFromSrams_bore_48_ram_aux_ckbp(sigFromSrams_bore_48_ram_aux_ckbp),
        .sigFromSrams_bore_48_ram_mcp_hold(sigFromSrams_bore_48_ram_mcp_hold),
        .sigFromSrams_bore_48_cgen(sigFromSrams_bore_48_cgen),
        .sigFromSrams_bore_49_ram_hold(sigFromSrams_bore_49_ram_hold),
        .sigFromSrams_bore_49_ram_bypass(sigFromSrams_bore_49_ram_bypass),
        .sigFromSrams_bore_49_ram_bp_clken(sigFromSrams_bore_49_ram_bp_clken),
        .sigFromSrams_bore_49_ram_aux_clk(sigFromSrams_bore_49_ram_aux_clk),
        .sigFromSrams_bore_49_ram_aux_ckbp(sigFromSrams_bore_49_ram_aux_ckbp),
        .sigFromSrams_bore_49_ram_mcp_hold(sigFromSrams_bore_49_ram_mcp_hold),
        .sigFromSrams_bore_49_cgen(sigFromSrams_bore_49_cgen),
        .sigFromSrams_bore_50_ram_hold(sigFromSrams_bore_50_ram_hold),
        .sigFromSrams_bore_50_ram_bypass(sigFromSrams_bore_50_ram_bypass),
        .sigFromSrams_bore_50_ram_bp_clken(sigFromSrams_bore_50_ram_bp_clken),
        .sigFromSrams_bore_50_ram_aux_clk(sigFromSrams_bore_50_ram_aux_clk),
        .sigFromSrams_bore_50_ram_aux_ckbp(sigFromSrams_bore_50_ram_aux_ckbp),
        .sigFromSrams_bore_50_ram_mcp_hold(sigFromSrams_bore_50_ram_mcp_hold),
        .sigFromSrams_bore_50_cgen(sigFromSrams_bore_50_cgen),
        .sigFromSrams_bore_51_ram_hold(sigFromSrams_bore_51_ram_hold),
        .sigFromSrams_bore_51_ram_bypass(sigFromSrams_bore_51_ram_bypass),
        .sigFromSrams_bore_51_ram_bp_clken(sigFromSrams_bore_51_ram_bp_clken),
        .sigFromSrams_bore_51_ram_aux_clk(sigFromSrams_bore_51_ram_aux_clk),
        .sigFromSrams_bore_51_ram_aux_ckbp(sigFromSrams_bore_51_ram_aux_ckbp),
        .sigFromSrams_bore_51_ram_mcp_hold(sigFromSrams_bore_51_ram_mcp_hold),
        .sigFromSrams_bore_51_cgen(sigFromSrams_bore_51_cgen),
        .sigFromSrams_bore_52_ram_hold(sigFromSrams_bore_52_ram_hold),
        .sigFromSrams_bore_52_ram_bypass(sigFromSrams_bore_52_ram_bypass),
        .sigFromSrams_bore_52_ram_bp_clken(sigFromSrams_bore_52_ram_bp_clken),
        .sigFromSrams_bore_52_ram_aux_clk(sigFromSrams_bore_52_ram_aux_clk),
        .sigFromSrams_bore_52_ram_aux_ckbp(sigFromSrams_bore_52_ram_aux_ckbp),
        .sigFromSrams_bore_52_ram_mcp_hold(sigFromSrams_bore_52_ram_mcp_hold),
        .sigFromSrams_bore_52_cgen(sigFromSrams_bore_52_cgen),
        .sigFromSrams_bore_53_ram_hold(sigFromSrams_bore_53_ram_hold),
        .sigFromSrams_bore_53_ram_bypass(sigFromSrams_bore_53_ram_bypass),
        .sigFromSrams_bore_53_ram_bp_clken(sigFromSrams_bore_53_ram_bp_clken),
        .sigFromSrams_bore_53_ram_aux_clk(sigFromSrams_bore_53_ram_aux_clk),
        .sigFromSrams_bore_53_ram_aux_ckbp(sigFromSrams_bore_53_ram_aux_ckbp),
        .sigFromSrams_bore_53_ram_mcp_hold(sigFromSrams_bore_53_ram_mcp_hold),
        .sigFromSrams_bore_53_cgen(sigFromSrams_bore_53_cgen),
        .sigFromSrams_bore_54_ram_hold(sigFromSrams_bore_54_ram_hold),
        .sigFromSrams_bore_54_ram_bypass(sigFromSrams_bore_54_ram_bypass),
        .sigFromSrams_bore_54_ram_bp_clken(sigFromSrams_bore_54_ram_bp_clken),
        .sigFromSrams_bore_54_ram_aux_clk(sigFromSrams_bore_54_ram_aux_clk),
        .sigFromSrams_bore_54_ram_aux_ckbp(sigFromSrams_bore_54_ram_aux_ckbp),
        .sigFromSrams_bore_54_ram_mcp_hold(sigFromSrams_bore_54_ram_mcp_hold),
        .sigFromSrams_bore_54_cgen(sigFromSrams_bore_54_cgen),
        .sigFromSrams_bore_55_ram_hold(sigFromSrams_bore_55_ram_hold),
        .sigFromSrams_bore_55_ram_bypass(sigFromSrams_bore_55_ram_bypass),
        .sigFromSrams_bore_55_ram_bp_clken(sigFromSrams_bore_55_ram_bp_clken),
        .sigFromSrams_bore_55_ram_aux_clk(sigFromSrams_bore_55_ram_aux_clk),
        .sigFromSrams_bore_55_ram_aux_ckbp(sigFromSrams_bore_55_ram_aux_ckbp),
        .sigFromSrams_bore_55_ram_mcp_hold(sigFromSrams_bore_55_ram_mcp_hold),
        .sigFromSrams_bore_55_cgen(sigFromSrams_bore_55_cgen),
        .sigFromSrams_bore_56_ram_hold(sigFromSrams_bore_56_ram_hold),
        .sigFromSrams_bore_56_ram_bypass(sigFromSrams_bore_56_ram_bypass),
        .sigFromSrams_bore_56_ram_bp_clken(sigFromSrams_bore_56_ram_bp_clken),
        .sigFromSrams_bore_56_ram_aux_clk(sigFromSrams_bore_56_ram_aux_clk),
        .sigFromSrams_bore_56_ram_aux_ckbp(sigFromSrams_bore_56_ram_aux_ckbp),
        .sigFromSrams_bore_56_ram_mcp_hold(sigFromSrams_bore_56_ram_mcp_hold),
        .sigFromSrams_bore_56_cgen(sigFromSrams_bore_56_cgen),
        .sigFromSrams_bore_57_ram_hold(sigFromSrams_bore_57_ram_hold),
        .sigFromSrams_bore_57_ram_bypass(sigFromSrams_bore_57_ram_bypass),
        .sigFromSrams_bore_57_ram_bp_clken(sigFromSrams_bore_57_ram_bp_clken),
        .sigFromSrams_bore_57_ram_aux_clk(sigFromSrams_bore_57_ram_aux_clk),
        .sigFromSrams_bore_57_ram_aux_ckbp(sigFromSrams_bore_57_ram_aux_ckbp),
        .sigFromSrams_bore_57_ram_mcp_hold(sigFromSrams_bore_57_ram_mcp_hold),
        .sigFromSrams_bore_57_cgen(sigFromSrams_bore_57_cgen),
        .sigFromSrams_bore_58_ram_hold(sigFromSrams_bore_58_ram_hold),
        .sigFromSrams_bore_58_ram_bypass(sigFromSrams_bore_58_ram_bypass),
        .sigFromSrams_bore_58_ram_bp_clken(sigFromSrams_bore_58_ram_bp_clken),
        .sigFromSrams_bore_58_ram_aux_clk(sigFromSrams_bore_58_ram_aux_clk),
        .sigFromSrams_bore_58_ram_aux_ckbp(sigFromSrams_bore_58_ram_aux_ckbp),
        .sigFromSrams_bore_58_ram_mcp_hold(sigFromSrams_bore_58_ram_mcp_hold),
        .sigFromSrams_bore_58_cgen(sigFromSrams_bore_58_cgen),
        .sigFromSrams_bore_59_ram_hold(sigFromSrams_bore_59_ram_hold),
        .sigFromSrams_bore_59_ram_bypass(sigFromSrams_bore_59_ram_bypass),
        .sigFromSrams_bore_59_ram_bp_clken(sigFromSrams_bore_59_ram_bp_clken),
        .sigFromSrams_bore_59_ram_aux_clk(sigFromSrams_bore_59_ram_aux_clk),
        .sigFromSrams_bore_59_ram_aux_ckbp(sigFromSrams_bore_59_ram_aux_ckbp),
        .sigFromSrams_bore_59_ram_mcp_hold(sigFromSrams_bore_59_ram_mcp_hold),
        .sigFromSrams_bore_59_cgen(sigFromSrams_bore_59_cgen),
        .sigFromSrams_bore_60_ram_hold(sigFromSrams_bore_60_ram_hold),
        .sigFromSrams_bore_60_ram_bypass(sigFromSrams_bore_60_ram_bypass),
        .sigFromSrams_bore_60_ram_bp_clken(sigFromSrams_bore_60_ram_bp_clken),
        .sigFromSrams_bore_60_ram_aux_clk(sigFromSrams_bore_60_ram_aux_clk),
        .sigFromSrams_bore_60_ram_aux_ckbp(sigFromSrams_bore_60_ram_aux_ckbp),
        .sigFromSrams_bore_60_ram_mcp_hold(sigFromSrams_bore_60_ram_mcp_hold),
        .sigFromSrams_bore_60_cgen(sigFromSrams_bore_60_cgen),
        .sigFromSrams_bore_61_ram_hold(sigFromSrams_bore_61_ram_hold),
        .sigFromSrams_bore_61_ram_bypass(sigFromSrams_bore_61_ram_bypass),
        .sigFromSrams_bore_61_ram_bp_clken(sigFromSrams_bore_61_ram_bp_clken),
        .sigFromSrams_bore_61_ram_aux_clk(sigFromSrams_bore_61_ram_aux_clk),
        .sigFromSrams_bore_61_ram_aux_ckbp(sigFromSrams_bore_61_ram_aux_ckbp),
        .sigFromSrams_bore_61_ram_mcp_hold(sigFromSrams_bore_61_ram_mcp_hold),
        .sigFromSrams_bore_61_cgen(sigFromSrams_bore_61_cgen),
        .sigFromSrams_bore_62_ram_hold(sigFromSrams_bore_62_ram_hold),
        .sigFromSrams_bore_62_ram_bypass(sigFromSrams_bore_62_ram_bypass),
        .sigFromSrams_bore_62_ram_bp_clken(sigFromSrams_bore_62_ram_bp_clken),
        .sigFromSrams_bore_62_ram_aux_clk(sigFromSrams_bore_62_ram_aux_clk),
        .sigFromSrams_bore_62_ram_aux_ckbp(sigFromSrams_bore_62_ram_aux_ckbp),
        .sigFromSrams_bore_62_ram_mcp_hold(sigFromSrams_bore_62_ram_mcp_hold),
        .sigFromSrams_bore_62_cgen(sigFromSrams_bore_62_cgen),
        .sigFromSrams_bore_63_ram_hold(sigFromSrams_bore_63_ram_hold),
        .sigFromSrams_bore_63_ram_bypass(sigFromSrams_bore_63_ram_bypass),
        .sigFromSrams_bore_63_ram_bp_clken(sigFromSrams_bore_63_ram_bp_clken),
        .sigFromSrams_bore_63_ram_aux_clk(sigFromSrams_bore_63_ram_aux_clk),
        .sigFromSrams_bore_63_ram_aux_ckbp(sigFromSrams_bore_63_ram_aux_ckbp),
        .sigFromSrams_bore_63_ram_mcp_hold(sigFromSrams_bore_63_ram_mcp_hold),
        .sigFromSrams_bore_63_cgen(sigFromSrams_bore_63_cgen),
        .sigFromSrams_bore_64_ram_hold(sigFromSrams_bore_64_ram_hold),
        .sigFromSrams_bore_64_ram_bypass(sigFromSrams_bore_64_ram_bypass),
        .sigFromSrams_bore_64_ram_bp_clken(sigFromSrams_bore_64_ram_bp_clken),
        .sigFromSrams_bore_64_ram_aux_clk(sigFromSrams_bore_64_ram_aux_clk),
        .sigFromSrams_bore_64_ram_aux_ckbp(sigFromSrams_bore_64_ram_aux_ckbp),
        .sigFromSrams_bore_64_ram_mcp_hold(sigFromSrams_bore_64_ram_mcp_hold),
        .sigFromSrams_bore_64_cgen(sigFromSrams_bore_64_cgen),
        .sigFromSrams_bore_65_ram_hold(sigFromSrams_bore_65_ram_hold),
        .sigFromSrams_bore_65_ram_bypass(sigFromSrams_bore_65_ram_bypass),
        .sigFromSrams_bore_65_ram_bp_clken(sigFromSrams_bore_65_ram_bp_clken),
        .sigFromSrams_bore_65_ram_aux_clk(sigFromSrams_bore_65_ram_aux_clk),
        .sigFromSrams_bore_65_ram_aux_ckbp(sigFromSrams_bore_65_ram_aux_ckbp),
        .sigFromSrams_bore_65_ram_mcp_hold(sigFromSrams_bore_65_ram_mcp_hold),
        .sigFromSrams_bore_65_cgen(sigFromSrams_bore_65_cgen),
        .sigFromSrams_bore_66_ram_hold(sigFromSrams_bore_66_ram_hold),
        .sigFromSrams_bore_66_ram_bypass(sigFromSrams_bore_66_ram_bypass),
        .sigFromSrams_bore_66_ram_bp_clken(sigFromSrams_bore_66_ram_bp_clken),
        .sigFromSrams_bore_66_ram_aux_clk(sigFromSrams_bore_66_ram_aux_clk),
        .sigFromSrams_bore_66_ram_aux_ckbp(sigFromSrams_bore_66_ram_aux_ckbp),
        .sigFromSrams_bore_66_ram_mcp_hold(sigFromSrams_bore_66_ram_mcp_hold),
        .sigFromSrams_bore_66_cgen(sigFromSrams_bore_66_cgen),
        .sigFromSrams_bore_67_ram_hold(sigFromSrams_bore_67_ram_hold),
        .sigFromSrams_bore_67_ram_bypass(sigFromSrams_bore_67_ram_bypass),
        .sigFromSrams_bore_67_ram_bp_clken(sigFromSrams_bore_67_ram_bp_clken),
        .sigFromSrams_bore_67_ram_aux_clk(sigFromSrams_bore_67_ram_aux_clk),
        .sigFromSrams_bore_67_ram_aux_ckbp(sigFromSrams_bore_67_ram_aux_ckbp),
        .sigFromSrams_bore_67_ram_mcp_hold(sigFromSrams_bore_67_ram_mcp_hold),
        .sigFromSrams_bore_67_cgen(sigFromSrams_bore_67_cgen),
        .sigFromSrams_bore_68_ram_hold(sigFromSrams_bore_68_ram_hold),
        .sigFromSrams_bore_68_ram_bypass(sigFromSrams_bore_68_ram_bypass),
        .sigFromSrams_bore_68_ram_bp_clken(sigFromSrams_bore_68_ram_bp_clken),
        .sigFromSrams_bore_68_ram_aux_clk(sigFromSrams_bore_68_ram_aux_clk),
        .sigFromSrams_bore_68_ram_aux_ckbp(sigFromSrams_bore_68_ram_aux_ckbp),
        .sigFromSrams_bore_68_ram_mcp_hold(sigFromSrams_bore_68_ram_mcp_hold),
        .sigFromSrams_bore_68_cgen(sigFromSrams_bore_68_cgen),
        .sigFromSrams_bore_69_ram_hold(sigFromSrams_bore_69_ram_hold),
        .sigFromSrams_bore_69_ram_bypass(sigFromSrams_bore_69_ram_bypass),
        .sigFromSrams_bore_69_ram_bp_clken(sigFromSrams_bore_69_ram_bp_clken),
        .sigFromSrams_bore_69_ram_aux_clk(sigFromSrams_bore_69_ram_aux_clk),
        .sigFromSrams_bore_69_ram_aux_ckbp(sigFromSrams_bore_69_ram_aux_ckbp),
        .sigFromSrams_bore_69_ram_mcp_hold(sigFromSrams_bore_69_ram_mcp_hold),
        .sigFromSrams_bore_69_cgen(sigFromSrams_bore_69_cgen),
        .sigFromSrams_bore_70_ram_hold(sigFromSrams_bore_70_ram_hold),
        .sigFromSrams_bore_70_ram_bypass(sigFromSrams_bore_70_ram_bypass),
        .sigFromSrams_bore_70_ram_bp_clken(sigFromSrams_bore_70_ram_bp_clken),
        .sigFromSrams_bore_70_ram_aux_clk(sigFromSrams_bore_70_ram_aux_clk),
        .sigFromSrams_bore_70_ram_aux_ckbp(sigFromSrams_bore_70_ram_aux_ckbp),
        .sigFromSrams_bore_70_ram_mcp_hold(sigFromSrams_bore_70_ram_mcp_hold),
        .sigFromSrams_bore_70_cgen(sigFromSrams_bore_70_cgen),
        .sigFromSrams_bore_71_ram_hold(sigFromSrams_bore_71_ram_hold),
        .sigFromSrams_bore_71_ram_bypass(sigFromSrams_bore_71_ram_bypass),
        .sigFromSrams_bore_71_ram_bp_clken(sigFromSrams_bore_71_ram_bp_clken),
        .sigFromSrams_bore_71_ram_aux_clk(sigFromSrams_bore_71_ram_aux_clk),
        .sigFromSrams_bore_71_ram_aux_ckbp(sigFromSrams_bore_71_ram_aux_ckbp),
        .sigFromSrams_bore_71_ram_mcp_hold(sigFromSrams_bore_71_ram_mcp_hold),
        .sigFromSrams_bore_71_cgen(sigFromSrams_bore_71_cgen),
        .sigFromSrams_bore_72_ram_hold(sigFromSrams_bore_72_ram_hold),
        .sigFromSrams_bore_72_ram_bypass(sigFromSrams_bore_72_ram_bypass),
        .sigFromSrams_bore_72_ram_bp_clken(sigFromSrams_bore_72_ram_bp_clken),
        .sigFromSrams_bore_72_ram_aux_clk(sigFromSrams_bore_72_ram_aux_clk),
        .sigFromSrams_bore_72_ram_aux_ckbp(sigFromSrams_bore_72_ram_aux_ckbp),
        .sigFromSrams_bore_72_ram_mcp_hold(sigFromSrams_bore_72_ram_mcp_hold),
        .sigFromSrams_bore_72_cgen(sigFromSrams_bore_72_cgen),
        .sigFromSrams_bore_73_ram_hold(sigFromSrams_bore_73_ram_hold),
        .sigFromSrams_bore_73_ram_bypass(sigFromSrams_bore_73_ram_bypass),
        .sigFromSrams_bore_73_ram_bp_clken(sigFromSrams_bore_73_ram_bp_clken),
        .sigFromSrams_bore_73_ram_aux_clk(sigFromSrams_bore_73_ram_aux_clk),
        .sigFromSrams_bore_73_ram_aux_ckbp(sigFromSrams_bore_73_ram_aux_ckbp),
        .sigFromSrams_bore_73_ram_mcp_hold(sigFromSrams_bore_73_ram_mcp_hold),
        .sigFromSrams_bore_73_cgen(sigFromSrams_bore_73_cgen),
        .sigFromSrams_bore_74_ram_hold(sigFromSrams_bore_74_ram_hold),
        .sigFromSrams_bore_74_ram_bypass(sigFromSrams_bore_74_ram_bypass),
        .sigFromSrams_bore_74_ram_bp_clken(sigFromSrams_bore_74_ram_bp_clken),
        .sigFromSrams_bore_74_ram_aux_clk(sigFromSrams_bore_74_ram_aux_clk),
        .sigFromSrams_bore_74_ram_aux_ckbp(sigFromSrams_bore_74_ram_aux_ckbp),
        .sigFromSrams_bore_74_ram_mcp_hold(sigFromSrams_bore_74_ram_mcp_hold),
        .sigFromSrams_bore_74_cgen(sigFromSrams_bore_74_cgen),
        .sigFromSrams_bore_75_ram_hold(sigFromSrams_bore_75_ram_hold),
        .sigFromSrams_bore_75_ram_bypass(sigFromSrams_bore_75_ram_bypass),
        .sigFromSrams_bore_75_ram_bp_clken(sigFromSrams_bore_75_ram_bp_clken),
        .sigFromSrams_bore_75_ram_aux_clk(sigFromSrams_bore_75_ram_aux_clk),
        .sigFromSrams_bore_75_ram_aux_ckbp(sigFromSrams_bore_75_ram_aux_ckbp),
        .sigFromSrams_bore_75_ram_mcp_hold(sigFromSrams_bore_75_ram_mcp_hold),
        .sigFromSrams_bore_75_cgen(sigFromSrams_bore_75_cgen),
        .sigFromSrams_bore_76_ram_hold(sigFromSrams_bore_76_ram_hold),
        .sigFromSrams_bore_76_ram_bypass(sigFromSrams_bore_76_ram_bypass),
        .sigFromSrams_bore_76_ram_bp_clken(sigFromSrams_bore_76_ram_bp_clken),
        .sigFromSrams_bore_76_ram_aux_clk(sigFromSrams_bore_76_ram_aux_clk),
        .sigFromSrams_bore_76_ram_aux_ckbp(sigFromSrams_bore_76_ram_aux_ckbp),
        .sigFromSrams_bore_76_ram_mcp_hold(sigFromSrams_bore_76_ram_mcp_hold),
        .sigFromSrams_bore_76_cgen(sigFromSrams_bore_76_cgen),
        .sigFromSrams_bore_77_ram_hold(sigFromSrams_bore_77_ram_hold),
        .sigFromSrams_bore_77_ram_bypass(sigFromSrams_bore_77_ram_bypass),
        .sigFromSrams_bore_77_ram_bp_clken(sigFromSrams_bore_77_ram_bp_clken),
        .sigFromSrams_bore_77_ram_aux_clk(sigFromSrams_bore_77_ram_aux_clk),
        .sigFromSrams_bore_77_ram_aux_ckbp(sigFromSrams_bore_77_ram_aux_ckbp),
        .sigFromSrams_bore_77_ram_mcp_hold(sigFromSrams_bore_77_ram_mcp_hold),
        .sigFromSrams_bore_77_cgen(sigFromSrams_bore_77_cgen),
        .sigFromSrams_bore_78_ram_hold(sigFromSrams_bore_78_ram_hold),
        .sigFromSrams_bore_78_ram_bypass(sigFromSrams_bore_78_ram_bypass),
        .sigFromSrams_bore_78_ram_bp_clken(sigFromSrams_bore_78_ram_bp_clken),
        .sigFromSrams_bore_78_ram_aux_clk(sigFromSrams_bore_78_ram_aux_clk),
        .sigFromSrams_bore_78_ram_aux_ckbp(sigFromSrams_bore_78_ram_aux_ckbp),
        .sigFromSrams_bore_78_ram_mcp_hold(sigFromSrams_bore_78_ram_mcp_hold),
        .sigFromSrams_bore_78_cgen(sigFromSrams_bore_78_cgen),
        .sigFromSrams_bore_79_ram_hold(sigFromSrams_bore_79_ram_hold),
        .sigFromSrams_bore_79_ram_bypass(sigFromSrams_bore_79_ram_bypass),
        .sigFromSrams_bore_79_ram_bp_clken(sigFromSrams_bore_79_ram_bp_clken),
        .sigFromSrams_bore_79_ram_aux_clk(sigFromSrams_bore_79_ram_aux_clk),
        .sigFromSrams_bore_79_ram_aux_ckbp(sigFromSrams_bore_79_ram_aux_ckbp),
        .sigFromSrams_bore_79_ram_mcp_hold(sigFromSrams_bore_79_ram_mcp_hold),
        .sigFromSrams_bore_79_cgen(sigFromSrams_bore_79_cgen),
        .sigFromSrams_bore_80_ram_hold(sigFromSrams_bore_80_ram_hold),
        .sigFromSrams_bore_80_ram_bypass(sigFromSrams_bore_80_ram_bypass),
        .sigFromSrams_bore_80_ram_bp_clken(sigFromSrams_bore_80_ram_bp_clken),
        .sigFromSrams_bore_80_ram_aux_clk(sigFromSrams_bore_80_ram_aux_clk),
        .sigFromSrams_bore_80_ram_aux_ckbp(sigFromSrams_bore_80_ram_aux_ckbp),
        .sigFromSrams_bore_80_ram_mcp_hold(sigFromSrams_bore_80_ram_mcp_hold),
        .sigFromSrams_bore_80_cgen(sigFromSrams_bore_80_cgen),
        .sigFromSrams_bore_81_ram_hold(sigFromSrams_bore_81_ram_hold),
        .sigFromSrams_bore_81_ram_bypass(sigFromSrams_bore_81_ram_bypass),
        .sigFromSrams_bore_81_ram_bp_clken(sigFromSrams_bore_81_ram_bp_clken),
        .sigFromSrams_bore_81_ram_aux_clk(sigFromSrams_bore_81_ram_aux_clk),
        .sigFromSrams_bore_81_ram_aux_ckbp(sigFromSrams_bore_81_ram_aux_ckbp),
        .sigFromSrams_bore_81_ram_mcp_hold(sigFromSrams_bore_81_ram_mcp_hold),
        .sigFromSrams_bore_81_cgen(sigFromSrams_bore_81_cgen),
        .sigFromSrams_bore_82_ram_hold(sigFromSrams_bore_82_ram_hold),
        .sigFromSrams_bore_82_ram_bypass(sigFromSrams_bore_82_ram_bypass),
        .sigFromSrams_bore_82_ram_bp_clken(sigFromSrams_bore_82_ram_bp_clken),
        .sigFromSrams_bore_82_ram_aux_clk(sigFromSrams_bore_82_ram_aux_clk),
        .sigFromSrams_bore_82_ram_aux_ckbp(sigFromSrams_bore_82_ram_aux_ckbp),
        .sigFromSrams_bore_82_ram_mcp_hold(sigFromSrams_bore_82_ram_mcp_hold),
        .sigFromSrams_bore_82_cgen(sigFromSrams_bore_82_cgen),
        .sigFromSrams_bore_83_ram_hold(sigFromSrams_bore_83_ram_hold),
        .sigFromSrams_bore_83_ram_bypass(sigFromSrams_bore_83_ram_bypass),
        .sigFromSrams_bore_83_ram_bp_clken(sigFromSrams_bore_83_ram_bp_clken),
        .sigFromSrams_bore_83_ram_aux_clk(sigFromSrams_bore_83_ram_aux_clk),
        .sigFromSrams_bore_83_ram_aux_ckbp(sigFromSrams_bore_83_ram_aux_ckbp),
        .sigFromSrams_bore_83_ram_mcp_hold(sigFromSrams_bore_83_ram_mcp_hold),
        .sigFromSrams_bore_83_cgen(sigFromSrams_bore_83_cgen),
        .sigFromSrams_bore_84_ram_hold(sigFromSrams_bore_84_ram_hold),
        .sigFromSrams_bore_84_ram_bypass(sigFromSrams_bore_84_ram_bypass),
        .sigFromSrams_bore_84_ram_bp_clken(sigFromSrams_bore_84_ram_bp_clken),
        .sigFromSrams_bore_84_ram_aux_clk(sigFromSrams_bore_84_ram_aux_clk),
        .sigFromSrams_bore_84_ram_aux_ckbp(sigFromSrams_bore_84_ram_aux_ckbp),
        .sigFromSrams_bore_84_ram_mcp_hold(sigFromSrams_bore_84_ram_mcp_hold),
        .sigFromSrams_bore_84_cgen(sigFromSrams_bore_84_cgen),
        .sigFromSrams_bore_85_ram_hold(sigFromSrams_bore_85_ram_hold),
        .sigFromSrams_bore_85_ram_bypass(sigFromSrams_bore_85_ram_bypass),
        .sigFromSrams_bore_85_ram_bp_clken(sigFromSrams_bore_85_ram_bp_clken),
        .sigFromSrams_bore_85_ram_aux_clk(sigFromSrams_bore_85_ram_aux_clk),
        .sigFromSrams_bore_85_ram_aux_ckbp(sigFromSrams_bore_85_ram_aux_ckbp),
        .sigFromSrams_bore_85_ram_mcp_hold(sigFromSrams_bore_85_ram_mcp_hold),
        .sigFromSrams_bore_85_cgen(sigFromSrams_bore_85_cgen),
        .sigFromSrams_bore_86_ram_hold(sigFromSrams_bore_86_ram_hold),
        .sigFromSrams_bore_86_ram_bypass(sigFromSrams_bore_86_ram_bypass),
        .sigFromSrams_bore_86_ram_bp_clken(sigFromSrams_bore_86_ram_bp_clken),
        .sigFromSrams_bore_86_ram_aux_clk(sigFromSrams_bore_86_ram_aux_clk),
        .sigFromSrams_bore_86_ram_aux_ckbp(sigFromSrams_bore_86_ram_aux_ckbp),
        .sigFromSrams_bore_86_ram_mcp_hold(sigFromSrams_bore_86_ram_mcp_hold),
        .sigFromSrams_bore_86_cgen(sigFromSrams_bore_86_cgen),
        .sigFromSrams_bore_87_ram_hold(sigFromSrams_bore_87_ram_hold),
        .sigFromSrams_bore_87_ram_bypass(sigFromSrams_bore_87_ram_bypass),
        .sigFromSrams_bore_87_ram_bp_clken(sigFromSrams_bore_87_ram_bp_clken),
        .sigFromSrams_bore_87_ram_aux_clk(sigFromSrams_bore_87_ram_aux_clk),
        .sigFromSrams_bore_87_ram_aux_ckbp(sigFromSrams_bore_87_ram_aux_ckbp),
        .sigFromSrams_bore_87_ram_mcp_hold(sigFromSrams_bore_87_ram_mcp_hold),
        .sigFromSrams_bore_87_cgen(sigFromSrams_bore_87_cgen),
        .sigFromSrams_bore_88_ram_hold(sigFromSrams_bore_88_ram_hold),
        .sigFromSrams_bore_88_ram_bypass(sigFromSrams_bore_88_ram_bypass),
        .sigFromSrams_bore_88_ram_bp_clken(sigFromSrams_bore_88_ram_bp_clken),
        .sigFromSrams_bore_88_ram_aux_clk(sigFromSrams_bore_88_ram_aux_clk),
        .sigFromSrams_bore_88_ram_aux_ckbp(sigFromSrams_bore_88_ram_aux_ckbp),
        .sigFromSrams_bore_88_ram_mcp_hold(sigFromSrams_bore_88_ram_mcp_hold),
        .sigFromSrams_bore_88_cgen(sigFromSrams_bore_88_cgen),
        .sigFromSrams_bore_89_ram_hold(sigFromSrams_bore_89_ram_hold),
        .sigFromSrams_bore_89_ram_bypass(sigFromSrams_bore_89_ram_bypass),
        .sigFromSrams_bore_89_ram_bp_clken(sigFromSrams_bore_89_ram_bp_clken),
        .sigFromSrams_bore_89_ram_aux_clk(sigFromSrams_bore_89_ram_aux_clk),
        .sigFromSrams_bore_89_ram_aux_ckbp(sigFromSrams_bore_89_ram_aux_ckbp),
        .sigFromSrams_bore_89_ram_mcp_hold(sigFromSrams_bore_89_ram_mcp_hold),
        .sigFromSrams_bore_89_cgen(sigFromSrams_bore_89_cgen),
        .sigFromSrams_bore_90_ram_hold(sigFromSrams_bore_90_ram_hold),
        .sigFromSrams_bore_90_ram_bypass(sigFromSrams_bore_90_ram_bypass),
        .sigFromSrams_bore_90_ram_bp_clken(sigFromSrams_bore_90_ram_bp_clken),
        .sigFromSrams_bore_90_ram_aux_clk(sigFromSrams_bore_90_ram_aux_clk),
        .sigFromSrams_bore_90_ram_aux_ckbp(sigFromSrams_bore_90_ram_aux_ckbp),
        .sigFromSrams_bore_90_ram_mcp_hold(sigFromSrams_bore_90_ram_mcp_hold),
        .sigFromSrams_bore_90_cgen(sigFromSrams_bore_90_cgen),
        .sigFromSrams_bore_91_ram_hold(sigFromSrams_bore_91_ram_hold),
        .sigFromSrams_bore_91_ram_bypass(sigFromSrams_bore_91_ram_bypass),
        .sigFromSrams_bore_91_ram_bp_clken(sigFromSrams_bore_91_ram_bp_clken),
        .sigFromSrams_bore_91_ram_aux_clk(sigFromSrams_bore_91_ram_aux_clk),
        .sigFromSrams_bore_91_ram_aux_ckbp(sigFromSrams_bore_91_ram_aux_ckbp),
        .sigFromSrams_bore_91_ram_mcp_hold(sigFromSrams_bore_91_ram_mcp_hold),
        .sigFromSrams_bore_91_cgen(sigFromSrams_bore_91_cgen),
        .sigFromSrams_bore_92_ram_hold(sigFromSrams_bore_92_ram_hold),
        .sigFromSrams_bore_92_ram_bypass(sigFromSrams_bore_92_ram_bypass),
        .sigFromSrams_bore_92_ram_bp_clken(sigFromSrams_bore_92_ram_bp_clken),
        .sigFromSrams_bore_92_ram_aux_clk(sigFromSrams_bore_92_ram_aux_clk),
        .sigFromSrams_bore_92_ram_aux_ckbp(sigFromSrams_bore_92_ram_aux_ckbp),
        .sigFromSrams_bore_92_ram_mcp_hold(sigFromSrams_bore_92_ram_mcp_hold),
        .sigFromSrams_bore_92_cgen(sigFromSrams_bore_92_cgen),
        .sigFromSrams_bore_93_ram_hold(sigFromSrams_bore_93_ram_hold),
        .sigFromSrams_bore_93_ram_bypass(sigFromSrams_bore_93_ram_bypass),
        .sigFromSrams_bore_93_ram_bp_clken(sigFromSrams_bore_93_ram_bp_clken),
        .sigFromSrams_bore_93_ram_aux_clk(sigFromSrams_bore_93_ram_aux_clk),
        .sigFromSrams_bore_93_ram_aux_ckbp(sigFromSrams_bore_93_ram_aux_ckbp),
        .sigFromSrams_bore_93_ram_mcp_hold(sigFromSrams_bore_93_ram_mcp_hold),
        .sigFromSrams_bore_93_cgen(sigFromSrams_bore_93_cgen),
        .sigFromSrams_bore_94_ram_hold(sigFromSrams_bore_94_ram_hold),
        .sigFromSrams_bore_94_ram_bypass(sigFromSrams_bore_94_ram_bypass),
        .sigFromSrams_bore_94_ram_bp_clken(sigFromSrams_bore_94_ram_bp_clken),
        .sigFromSrams_bore_94_ram_aux_clk(sigFromSrams_bore_94_ram_aux_clk),
        .sigFromSrams_bore_94_ram_aux_ckbp(sigFromSrams_bore_94_ram_aux_ckbp),
        .sigFromSrams_bore_94_ram_mcp_hold(sigFromSrams_bore_94_ram_mcp_hold),
        .sigFromSrams_bore_94_cgen(sigFromSrams_bore_94_cgen),
        .sigFromSrams_bore_95_ram_hold(sigFromSrams_bore_95_ram_hold),
        .sigFromSrams_bore_95_ram_bypass(sigFromSrams_bore_95_ram_bypass),
        .sigFromSrams_bore_95_ram_bp_clken(sigFromSrams_bore_95_ram_bp_clken),
        .sigFromSrams_bore_95_ram_aux_clk(sigFromSrams_bore_95_ram_aux_clk),
        .sigFromSrams_bore_95_ram_aux_ckbp(sigFromSrams_bore_95_ram_aux_ckbp),
        .sigFromSrams_bore_95_ram_mcp_hold(sigFromSrams_bore_95_ram_mcp_hold),
        .sigFromSrams_bore_95_cgen(sigFromSrams_bore_95_cgen),
        .sigFromSrams_bore_96_ram_hold(sigFromSrams_bore_96_ram_hold),
        .sigFromSrams_bore_96_ram_bypass(sigFromSrams_bore_96_ram_bypass),
        .sigFromSrams_bore_96_ram_bp_clken(sigFromSrams_bore_96_ram_bp_clken),
        .sigFromSrams_bore_96_ram_aux_clk(sigFromSrams_bore_96_ram_aux_clk),
        .sigFromSrams_bore_96_ram_aux_ckbp(sigFromSrams_bore_96_ram_aux_ckbp),
        .sigFromSrams_bore_96_ram_mcp_hold(sigFromSrams_bore_96_ram_mcp_hold),
        .sigFromSrams_bore_96_cgen(sigFromSrams_bore_96_cgen),
        .sigFromSrams_bore_97_ram_hold(sigFromSrams_bore_97_ram_hold),
        .sigFromSrams_bore_97_ram_bypass(sigFromSrams_bore_97_ram_bypass),
        .sigFromSrams_bore_97_ram_bp_clken(sigFromSrams_bore_97_ram_bp_clken),
        .sigFromSrams_bore_97_ram_aux_clk(sigFromSrams_bore_97_ram_aux_clk),
        .sigFromSrams_bore_97_ram_aux_ckbp(sigFromSrams_bore_97_ram_aux_ckbp),
        .sigFromSrams_bore_97_ram_mcp_hold(sigFromSrams_bore_97_ram_mcp_hold),
        .sigFromSrams_bore_97_cgen(sigFromSrams_bore_97_cgen),
        .sigFromSrams_bore_98_ram_hold(sigFromSrams_bore_98_ram_hold),
        .sigFromSrams_bore_98_ram_bypass(sigFromSrams_bore_98_ram_bypass),
        .sigFromSrams_bore_98_ram_bp_clken(sigFromSrams_bore_98_ram_bp_clken),
        .sigFromSrams_bore_98_ram_aux_clk(sigFromSrams_bore_98_ram_aux_clk),
        .sigFromSrams_bore_98_ram_aux_ckbp(sigFromSrams_bore_98_ram_aux_ckbp),
        .sigFromSrams_bore_98_ram_mcp_hold(sigFromSrams_bore_98_ram_mcp_hold),
        .sigFromSrams_bore_98_cgen(sigFromSrams_bore_98_cgen),
        .sigFromSrams_bore_99_ram_hold(sigFromSrams_bore_99_ram_hold),
        .sigFromSrams_bore_99_ram_bypass(sigFromSrams_bore_99_ram_bypass),
        .sigFromSrams_bore_99_ram_bp_clken(sigFromSrams_bore_99_ram_bp_clken),
        .sigFromSrams_bore_99_ram_aux_clk(sigFromSrams_bore_99_ram_aux_clk),
        .sigFromSrams_bore_99_ram_aux_ckbp(sigFromSrams_bore_99_ram_aux_ckbp),
        .sigFromSrams_bore_99_ram_mcp_hold(sigFromSrams_bore_99_ram_mcp_hold),
        .sigFromSrams_bore_99_cgen(sigFromSrams_bore_99_cgen),
        .sigFromSrams_bore_100_ram_hold(sigFromSrams_bore_100_ram_hold),
        .sigFromSrams_bore_100_ram_bypass(sigFromSrams_bore_100_ram_bypass),
        .sigFromSrams_bore_100_ram_bp_clken(sigFromSrams_bore_100_ram_bp_clken),
        .sigFromSrams_bore_100_ram_aux_clk(sigFromSrams_bore_100_ram_aux_clk),
        .sigFromSrams_bore_100_ram_aux_ckbp(sigFromSrams_bore_100_ram_aux_ckbp),
        .sigFromSrams_bore_100_ram_mcp_hold(sigFromSrams_bore_100_ram_mcp_hold),
        .sigFromSrams_bore_100_cgen(sigFromSrams_bore_100_cgen),
        .sigFromSrams_bore_101_ram_hold(sigFromSrams_bore_101_ram_hold),
        .sigFromSrams_bore_101_ram_bypass(sigFromSrams_bore_101_ram_bypass),
        .sigFromSrams_bore_101_ram_bp_clken(sigFromSrams_bore_101_ram_bp_clken),
        .sigFromSrams_bore_101_ram_aux_clk(sigFromSrams_bore_101_ram_aux_clk),
        .sigFromSrams_bore_101_ram_aux_ckbp(sigFromSrams_bore_101_ram_aux_ckbp),
        .sigFromSrams_bore_101_ram_mcp_hold(sigFromSrams_bore_101_ram_mcp_hold),
        .sigFromSrams_bore_101_cgen(sigFromSrams_bore_101_cgen),
        .sigFromSrams_bore_102_ram_hold(sigFromSrams_bore_102_ram_hold),
        .sigFromSrams_bore_102_ram_bypass(sigFromSrams_bore_102_ram_bypass),
        .sigFromSrams_bore_102_ram_bp_clken(sigFromSrams_bore_102_ram_bp_clken),
        .sigFromSrams_bore_102_ram_aux_clk(sigFromSrams_bore_102_ram_aux_clk),
        .sigFromSrams_bore_102_ram_aux_ckbp(sigFromSrams_bore_102_ram_aux_ckbp),
        .sigFromSrams_bore_102_ram_mcp_hold(sigFromSrams_bore_102_ram_mcp_hold),
        .sigFromSrams_bore_102_cgen(sigFromSrams_bore_102_cgen),
        .sigFromSrams_bore_103_ram_hold(sigFromSrams_bore_103_ram_hold),
        .sigFromSrams_bore_103_ram_bypass(sigFromSrams_bore_103_ram_bypass),
        .sigFromSrams_bore_103_ram_bp_clken(sigFromSrams_bore_103_ram_bp_clken),
        .sigFromSrams_bore_103_ram_aux_clk(sigFromSrams_bore_103_ram_aux_clk),
        .sigFromSrams_bore_103_ram_aux_ckbp(sigFromSrams_bore_103_ram_aux_ckbp),
        .sigFromSrams_bore_103_ram_mcp_hold(sigFromSrams_bore_103_ram_mcp_hold),
        .sigFromSrams_bore_103_cgen(sigFromSrams_bore_103_cgen),
        .sigFromSrams_bore_104_ram_hold(sigFromSrams_bore_104_ram_hold),
        .sigFromSrams_bore_104_ram_bypass(sigFromSrams_bore_104_ram_bypass),
        .sigFromSrams_bore_104_ram_bp_clken(sigFromSrams_bore_104_ram_bp_clken),
        .sigFromSrams_bore_104_ram_aux_clk(sigFromSrams_bore_104_ram_aux_clk),
        .sigFromSrams_bore_104_ram_aux_ckbp(sigFromSrams_bore_104_ram_aux_ckbp),
        .sigFromSrams_bore_104_ram_mcp_hold(sigFromSrams_bore_104_ram_mcp_hold),
        .sigFromSrams_bore_104_cgen(sigFromSrams_bore_104_cgen),
        .sigFromSrams_bore_105_ram_hold(sigFromSrams_bore_105_ram_hold),
        .sigFromSrams_bore_105_ram_bypass(sigFromSrams_bore_105_ram_bypass),
        .sigFromSrams_bore_105_ram_bp_clken(sigFromSrams_bore_105_ram_bp_clken),
        .sigFromSrams_bore_105_ram_aux_clk(sigFromSrams_bore_105_ram_aux_clk),
        .sigFromSrams_bore_105_ram_aux_ckbp(sigFromSrams_bore_105_ram_aux_ckbp),
        .sigFromSrams_bore_105_ram_mcp_hold(sigFromSrams_bore_105_ram_mcp_hold),
        .sigFromSrams_bore_105_cgen(sigFromSrams_bore_105_cgen),
        .sigFromSrams_bore_106_ram_hold(sigFromSrams_bore_106_ram_hold),
        .sigFromSrams_bore_106_ram_bypass(sigFromSrams_bore_106_ram_bypass),
        .sigFromSrams_bore_106_ram_bp_clken(sigFromSrams_bore_106_ram_bp_clken),
        .sigFromSrams_bore_106_ram_aux_clk(sigFromSrams_bore_106_ram_aux_clk),
        .sigFromSrams_bore_106_ram_aux_ckbp(sigFromSrams_bore_106_ram_aux_ckbp),
        .sigFromSrams_bore_106_ram_mcp_hold(sigFromSrams_bore_106_ram_mcp_hold),
        .sigFromSrams_bore_106_cgen(sigFromSrams_bore_106_cgen),
        .sigFromSrams_bore_107_ram_hold(sigFromSrams_bore_107_ram_hold),
        .sigFromSrams_bore_107_ram_bypass(sigFromSrams_bore_107_ram_bypass),
        .sigFromSrams_bore_107_ram_bp_clken(sigFromSrams_bore_107_ram_bp_clken),
        .sigFromSrams_bore_107_ram_aux_clk(sigFromSrams_bore_107_ram_aux_clk),
        .sigFromSrams_bore_107_ram_aux_ckbp(sigFromSrams_bore_107_ram_aux_ckbp),
        .sigFromSrams_bore_107_ram_mcp_hold(sigFromSrams_bore_107_ram_mcp_hold),
        .sigFromSrams_bore_107_cgen(sigFromSrams_bore_107_cgen),
        .sigFromSrams_bore_108_ram_hold(sigFromSrams_bore_108_ram_hold),
        .sigFromSrams_bore_108_ram_bypass(sigFromSrams_bore_108_ram_bypass),
        .sigFromSrams_bore_108_ram_bp_clken(sigFromSrams_bore_108_ram_bp_clken),
        .sigFromSrams_bore_108_ram_aux_clk(sigFromSrams_bore_108_ram_aux_clk),
        .sigFromSrams_bore_108_ram_aux_ckbp(sigFromSrams_bore_108_ram_aux_ckbp),
        .sigFromSrams_bore_108_ram_mcp_hold(sigFromSrams_bore_108_ram_mcp_hold),
        .sigFromSrams_bore_108_cgen(sigFromSrams_bore_108_cgen),
        .sigFromSrams_bore_109_ram_hold(sigFromSrams_bore_109_ram_hold),
        .sigFromSrams_bore_109_ram_bypass(sigFromSrams_bore_109_ram_bypass),
        .sigFromSrams_bore_109_ram_bp_clken(sigFromSrams_bore_109_ram_bp_clken),
        .sigFromSrams_bore_109_ram_aux_clk(sigFromSrams_bore_109_ram_aux_clk),
        .sigFromSrams_bore_109_ram_aux_ckbp(sigFromSrams_bore_109_ram_aux_ckbp),
        .sigFromSrams_bore_109_ram_mcp_hold(sigFromSrams_bore_109_ram_mcp_hold),
        .sigFromSrams_bore_109_cgen(sigFromSrams_bore_109_cgen),
        .sigFromSrams_bore_110_ram_hold(sigFromSrams_bore_110_ram_hold),
        .sigFromSrams_bore_110_ram_bypass(sigFromSrams_bore_110_ram_bypass),
        .sigFromSrams_bore_110_ram_bp_clken(sigFromSrams_bore_110_ram_bp_clken),
        .sigFromSrams_bore_110_ram_aux_clk(sigFromSrams_bore_110_ram_aux_clk),
        .sigFromSrams_bore_110_ram_aux_ckbp(sigFromSrams_bore_110_ram_aux_ckbp),
        .sigFromSrams_bore_110_ram_mcp_hold(sigFromSrams_bore_110_ram_mcp_hold),
        .sigFromSrams_bore_110_cgen(sigFromSrams_bore_110_cgen),
        .sigFromSrams_bore_111_ram_hold(sigFromSrams_bore_111_ram_hold),
        .sigFromSrams_bore_111_ram_bypass(sigFromSrams_bore_111_ram_bypass),
        .sigFromSrams_bore_111_ram_bp_clken(sigFromSrams_bore_111_ram_bp_clken),
        .sigFromSrams_bore_111_ram_aux_clk(sigFromSrams_bore_111_ram_aux_clk),
        .sigFromSrams_bore_111_ram_aux_ckbp(sigFromSrams_bore_111_ram_aux_ckbp),
        .sigFromSrams_bore_111_ram_mcp_hold(sigFromSrams_bore_111_ram_mcp_hold),
        .sigFromSrams_bore_111_cgen(sigFromSrams_bore_111_cgen),
        .sigFromSrams_bore_112_ram_hold(sigFromSrams_bore_112_ram_hold),
        .sigFromSrams_bore_112_ram_bypass(sigFromSrams_bore_112_ram_bypass),
        .sigFromSrams_bore_112_ram_bp_clken(sigFromSrams_bore_112_ram_bp_clken),
        .sigFromSrams_bore_112_ram_aux_clk(sigFromSrams_bore_112_ram_aux_clk),
        .sigFromSrams_bore_112_ram_aux_ckbp(sigFromSrams_bore_112_ram_aux_ckbp),
        .sigFromSrams_bore_112_ram_mcp_hold(sigFromSrams_bore_112_ram_mcp_hold),
        .sigFromSrams_bore_112_cgen(sigFromSrams_bore_112_cgen),
        .sigFromSrams_bore_113_ram_hold(sigFromSrams_bore_113_ram_hold),
        .sigFromSrams_bore_113_ram_bypass(sigFromSrams_bore_113_ram_bypass),
        .sigFromSrams_bore_113_ram_bp_clken(sigFromSrams_bore_113_ram_bp_clken),
        .sigFromSrams_bore_113_ram_aux_clk(sigFromSrams_bore_113_ram_aux_clk),
        .sigFromSrams_bore_113_ram_aux_ckbp(sigFromSrams_bore_113_ram_aux_ckbp),
        .sigFromSrams_bore_113_ram_mcp_hold(sigFromSrams_bore_113_ram_mcp_hold),
        .sigFromSrams_bore_113_cgen(sigFromSrams_bore_113_cgen),
        .sigFromSrams_bore_114_ram_hold(sigFromSrams_bore_114_ram_hold),
        .sigFromSrams_bore_114_ram_bypass(sigFromSrams_bore_114_ram_bypass),
        .sigFromSrams_bore_114_ram_bp_clken(sigFromSrams_bore_114_ram_bp_clken),
        .sigFromSrams_bore_114_ram_aux_clk(sigFromSrams_bore_114_ram_aux_clk),
        .sigFromSrams_bore_114_ram_aux_ckbp(sigFromSrams_bore_114_ram_aux_ckbp),
        .sigFromSrams_bore_114_ram_mcp_hold(sigFromSrams_bore_114_ram_mcp_hold),
        .sigFromSrams_bore_114_cgen(sigFromSrams_bore_114_cgen),
        .sigFromSrams_bore_115_ram_hold(sigFromSrams_bore_115_ram_hold),
        .sigFromSrams_bore_115_ram_bypass(sigFromSrams_bore_115_ram_bypass),
        .sigFromSrams_bore_115_ram_bp_clken(sigFromSrams_bore_115_ram_bp_clken),
        .sigFromSrams_bore_115_ram_aux_clk(sigFromSrams_bore_115_ram_aux_clk),
        .sigFromSrams_bore_115_ram_aux_ckbp(sigFromSrams_bore_115_ram_aux_ckbp),
        .sigFromSrams_bore_115_ram_mcp_hold(sigFromSrams_bore_115_ram_mcp_hold),
        .sigFromSrams_bore_115_cgen(sigFromSrams_bore_115_cgen),
        .sigFromSrams_bore_116_ram_hold(sigFromSrams_bore_116_ram_hold),
        .sigFromSrams_bore_116_ram_bypass(sigFromSrams_bore_116_ram_bypass),
        .sigFromSrams_bore_116_ram_bp_clken(sigFromSrams_bore_116_ram_bp_clken),
        .sigFromSrams_bore_116_ram_aux_clk(sigFromSrams_bore_116_ram_aux_clk),
        .sigFromSrams_bore_116_ram_aux_ckbp(sigFromSrams_bore_116_ram_aux_ckbp),
        .sigFromSrams_bore_116_ram_mcp_hold(sigFromSrams_bore_116_ram_mcp_hold),
        .sigFromSrams_bore_116_cgen(sigFromSrams_bore_116_cgen),
        .sigFromSrams_bore_117_ram_hold(sigFromSrams_bore_117_ram_hold),
        .sigFromSrams_bore_117_ram_bypass(sigFromSrams_bore_117_ram_bypass),
        .sigFromSrams_bore_117_ram_bp_clken(sigFromSrams_bore_117_ram_bp_clken),
        .sigFromSrams_bore_117_ram_aux_clk(sigFromSrams_bore_117_ram_aux_clk),
        .sigFromSrams_bore_117_ram_aux_ckbp(sigFromSrams_bore_117_ram_aux_ckbp),
        .sigFromSrams_bore_117_ram_mcp_hold(sigFromSrams_bore_117_ram_mcp_hold),
        .sigFromSrams_bore_117_cgen(sigFromSrams_bore_117_cgen),
        .sigFromSrams_bore_118_ram_hold(sigFromSrams_bore_118_ram_hold),
        .sigFromSrams_bore_118_ram_bypass(sigFromSrams_bore_118_ram_bypass),
        .sigFromSrams_bore_118_ram_bp_clken(sigFromSrams_bore_118_ram_bp_clken),
        .sigFromSrams_bore_118_ram_aux_clk(sigFromSrams_bore_118_ram_aux_clk),
        .sigFromSrams_bore_118_ram_aux_ckbp(sigFromSrams_bore_118_ram_aux_ckbp),
        .sigFromSrams_bore_118_ram_mcp_hold(sigFromSrams_bore_118_ram_mcp_hold),
        .sigFromSrams_bore_118_cgen(sigFromSrams_bore_118_cgen),
        .sigFromSrams_bore_119_ram_hold(sigFromSrams_bore_119_ram_hold),
        .sigFromSrams_bore_119_ram_bypass(sigFromSrams_bore_119_ram_bypass),
        .sigFromSrams_bore_119_ram_bp_clken(sigFromSrams_bore_119_ram_bp_clken),
        .sigFromSrams_bore_119_ram_aux_clk(sigFromSrams_bore_119_ram_aux_clk),
        .sigFromSrams_bore_119_ram_aux_ckbp(sigFromSrams_bore_119_ram_aux_ckbp),
        .sigFromSrams_bore_119_ram_mcp_hold(sigFromSrams_bore_119_ram_mcp_hold),
        .sigFromSrams_bore_119_cgen(sigFromSrams_bore_119_cgen),
        .sigFromSrams_bore_120_ram_hold(sigFromSrams_bore_120_ram_hold),
        .sigFromSrams_bore_120_ram_bypass(sigFromSrams_bore_120_ram_bypass),
        .sigFromSrams_bore_120_ram_bp_clken(sigFromSrams_bore_120_ram_bp_clken),
        .sigFromSrams_bore_120_ram_aux_clk(sigFromSrams_bore_120_ram_aux_clk),
        .sigFromSrams_bore_120_ram_aux_ckbp(sigFromSrams_bore_120_ram_aux_ckbp),
        .sigFromSrams_bore_120_ram_mcp_hold(sigFromSrams_bore_120_ram_mcp_hold),
        .sigFromSrams_bore_120_cgen(sigFromSrams_bore_120_cgen),
        .sigFromSrams_bore_121_ram_hold(sigFromSrams_bore_121_ram_hold),
        .sigFromSrams_bore_121_ram_bypass(sigFromSrams_bore_121_ram_bypass),
        .sigFromSrams_bore_121_ram_bp_clken(sigFromSrams_bore_121_ram_bp_clken),
        .sigFromSrams_bore_121_ram_aux_clk(sigFromSrams_bore_121_ram_aux_clk),
        .sigFromSrams_bore_121_ram_aux_ckbp(sigFromSrams_bore_121_ram_aux_ckbp),
        .sigFromSrams_bore_121_ram_mcp_hold(sigFromSrams_bore_121_ram_mcp_hold),
        .sigFromSrams_bore_121_cgen(sigFromSrams_bore_121_cgen),
        .sigFromSrams_bore_122_ram_hold(sigFromSrams_bore_122_ram_hold),
        .sigFromSrams_bore_122_ram_bypass(sigFromSrams_bore_122_ram_bypass),
        .sigFromSrams_bore_122_ram_bp_clken(sigFromSrams_bore_122_ram_bp_clken),
        .sigFromSrams_bore_122_ram_aux_clk(sigFromSrams_bore_122_ram_aux_clk),
        .sigFromSrams_bore_122_ram_aux_ckbp(sigFromSrams_bore_122_ram_aux_ckbp),
        .sigFromSrams_bore_122_ram_mcp_hold(sigFromSrams_bore_122_ram_mcp_hold),
        .sigFromSrams_bore_122_cgen(sigFromSrams_bore_122_cgen),
        .sigFromSrams_bore_123_ram_hold(sigFromSrams_bore_123_ram_hold),
        .sigFromSrams_bore_123_ram_bypass(sigFromSrams_bore_123_ram_bypass),
        .sigFromSrams_bore_123_ram_bp_clken(sigFromSrams_bore_123_ram_bp_clken),
        .sigFromSrams_bore_123_ram_aux_clk(sigFromSrams_bore_123_ram_aux_clk),
        .sigFromSrams_bore_123_ram_aux_ckbp(sigFromSrams_bore_123_ram_aux_ckbp),
        .sigFromSrams_bore_123_ram_mcp_hold(sigFromSrams_bore_123_ram_mcp_hold),
        .sigFromSrams_bore_123_cgen(sigFromSrams_bore_123_cgen),
        .sigFromSrams_bore_124_ram_hold(sigFromSrams_bore_124_ram_hold),
        .sigFromSrams_bore_124_ram_bypass(sigFromSrams_bore_124_ram_bypass),
        .sigFromSrams_bore_124_ram_bp_clken(sigFromSrams_bore_124_ram_bp_clken),
        .sigFromSrams_bore_124_ram_aux_clk(sigFromSrams_bore_124_ram_aux_clk),
        .sigFromSrams_bore_124_ram_aux_ckbp(sigFromSrams_bore_124_ram_aux_ckbp),
        .sigFromSrams_bore_124_ram_mcp_hold(sigFromSrams_bore_124_ram_mcp_hold),
        .sigFromSrams_bore_124_cgen(sigFromSrams_bore_124_cgen),
        .sigFromSrams_bore_125_ram_hold(sigFromSrams_bore_125_ram_hold),
        .sigFromSrams_bore_125_ram_bypass(sigFromSrams_bore_125_ram_bypass),
        .sigFromSrams_bore_125_ram_bp_clken(sigFromSrams_bore_125_ram_bp_clken),
        .sigFromSrams_bore_125_ram_aux_clk(sigFromSrams_bore_125_ram_aux_clk),
        .sigFromSrams_bore_125_ram_aux_ckbp(sigFromSrams_bore_125_ram_aux_ckbp),
        .sigFromSrams_bore_125_ram_mcp_hold(sigFromSrams_bore_125_ram_mcp_hold),
        .sigFromSrams_bore_125_cgen(sigFromSrams_bore_125_cgen),
        .sigFromSrams_bore_126_ram_hold(sigFromSrams_bore_126_ram_hold),
        .sigFromSrams_bore_126_ram_bypass(sigFromSrams_bore_126_ram_bypass),
        .sigFromSrams_bore_126_ram_bp_clken(sigFromSrams_bore_126_ram_bp_clken),
        .sigFromSrams_bore_126_ram_aux_clk(sigFromSrams_bore_126_ram_aux_clk),
        .sigFromSrams_bore_126_ram_aux_ckbp(sigFromSrams_bore_126_ram_aux_ckbp),
        .sigFromSrams_bore_126_ram_mcp_hold(sigFromSrams_bore_126_ram_mcp_hold),
        .sigFromSrams_bore_126_cgen(sigFromSrams_bore_126_cgen),
        .sigFromSrams_bore_127_ram_hold(sigFromSrams_bore_127_ram_hold),
        .sigFromSrams_bore_127_ram_bypass(sigFromSrams_bore_127_ram_bypass),
        .sigFromSrams_bore_127_ram_bp_clken(sigFromSrams_bore_127_ram_bp_clken),
        .sigFromSrams_bore_127_ram_aux_clk(sigFromSrams_bore_127_ram_aux_clk),
        .sigFromSrams_bore_127_ram_aux_ckbp(sigFromSrams_bore_127_ram_aux_ckbp),
        .sigFromSrams_bore_127_ram_mcp_hold(sigFromSrams_bore_127_ram_mcp_hold),
        .sigFromSrams_bore_127_cgen(sigFromSrams_bore_127_cgen),
        .sigFromSrams_bore_128_ram_hold(sigFromSrams_bore_128_ram_hold),
        .sigFromSrams_bore_128_ram_bypass(sigFromSrams_bore_128_ram_bypass),
        .sigFromSrams_bore_128_ram_bp_clken(sigFromSrams_bore_128_ram_bp_clken),
        .sigFromSrams_bore_128_ram_aux_clk(sigFromSrams_bore_128_ram_aux_clk),
        .sigFromSrams_bore_128_ram_aux_ckbp(sigFromSrams_bore_128_ram_aux_ckbp),
        .sigFromSrams_bore_128_ram_mcp_hold(sigFromSrams_bore_128_ram_mcp_hold),
        .sigFromSrams_bore_128_cgen(sigFromSrams_bore_128_cgen),
        .sigFromSrams_bore_129_ram_hold(sigFromSrams_bore_129_ram_hold),
        .sigFromSrams_bore_129_ram_bypass(sigFromSrams_bore_129_ram_bypass),
        .sigFromSrams_bore_129_ram_bp_clken(sigFromSrams_bore_129_ram_bp_clken),
        .sigFromSrams_bore_129_ram_aux_clk(sigFromSrams_bore_129_ram_aux_clk),
        .sigFromSrams_bore_129_ram_aux_ckbp(sigFromSrams_bore_129_ram_aux_ckbp),
        .sigFromSrams_bore_129_ram_mcp_hold(sigFromSrams_bore_129_ram_mcp_hold),
        .sigFromSrams_bore_129_cgen(sigFromSrams_bore_129_cgen),
        .sigFromSrams_bore_130_ram_hold(sigFromSrams_bore_130_ram_hold),
        .sigFromSrams_bore_130_ram_bypass(sigFromSrams_bore_130_ram_bypass),
        .sigFromSrams_bore_130_ram_bp_clken(sigFromSrams_bore_130_ram_bp_clken),
        .sigFromSrams_bore_130_ram_aux_clk(sigFromSrams_bore_130_ram_aux_clk),
        .sigFromSrams_bore_130_ram_aux_ckbp(sigFromSrams_bore_130_ram_aux_ckbp),
        .sigFromSrams_bore_130_ram_mcp_hold(sigFromSrams_bore_130_ram_mcp_hold),
        .sigFromSrams_bore_130_cgen(sigFromSrams_bore_130_cgen),
        .sigFromSrams_bore_131_ram_hold(sigFromSrams_bore_131_ram_hold),
        .sigFromSrams_bore_131_ram_bypass(sigFromSrams_bore_131_ram_bypass),
        .sigFromSrams_bore_131_ram_bp_clken(sigFromSrams_bore_131_ram_bp_clken),
        .sigFromSrams_bore_131_ram_aux_clk(sigFromSrams_bore_131_ram_aux_clk),
        .sigFromSrams_bore_131_ram_aux_ckbp(sigFromSrams_bore_131_ram_aux_ckbp),
        .sigFromSrams_bore_131_ram_mcp_hold(sigFromSrams_bore_131_ram_mcp_hold),
        .sigFromSrams_bore_131_cgen(sigFromSrams_bore_131_cgen),
        .sigFromSrams_bore_132_ram_hold(sigFromSrams_bore_132_ram_hold),
        .sigFromSrams_bore_132_ram_bypass(sigFromSrams_bore_132_ram_bypass),
        .sigFromSrams_bore_132_ram_bp_clken(sigFromSrams_bore_132_ram_bp_clken),
        .sigFromSrams_bore_132_ram_aux_clk(sigFromSrams_bore_132_ram_aux_clk),
        .sigFromSrams_bore_132_ram_aux_ckbp(sigFromSrams_bore_132_ram_aux_ckbp),
        .sigFromSrams_bore_132_ram_mcp_hold(sigFromSrams_bore_132_ram_mcp_hold),
        .sigFromSrams_bore_132_cgen(sigFromSrams_bore_132_cgen),
        .sigFromSrams_bore_133_ram_hold(sigFromSrams_bore_133_ram_hold),
        .sigFromSrams_bore_133_ram_bypass(sigFromSrams_bore_133_ram_bypass),
        .sigFromSrams_bore_133_ram_bp_clken(sigFromSrams_bore_133_ram_bp_clken),
        .sigFromSrams_bore_133_ram_aux_clk(sigFromSrams_bore_133_ram_aux_clk),
        .sigFromSrams_bore_133_ram_aux_ckbp(sigFromSrams_bore_133_ram_aux_ckbp),
        .sigFromSrams_bore_133_ram_mcp_hold(sigFromSrams_bore_133_ram_mcp_hold),
        .sigFromSrams_bore_133_cgen(sigFromSrams_bore_133_cgen),
        .sigFromSrams_bore_134_ram_hold(sigFromSrams_bore_134_ram_hold),
        .sigFromSrams_bore_134_ram_bypass(sigFromSrams_bore_134_ram_bypass),
        .sigFromSrams_bore_134_ram_bp_clken(sigFromSrams_bore_134_ram_bp_clken),
        .sigFromSrams_bore_134_ram_aux_clk(sigFromSrams_bore_134_ram_aux_clk),
        .sigFromSrams_bore_134_ram_aux_ckbp(sigFromSrams_bore_134_ram_aux_ckbp),
        .sigFromSrams_bore_134_ram_mcp_hold(sigFromSrams_bore_134_ram_mcp_hold),
        .sigFromSrams_bore_134_cgen(sigFromSrams_bore_134_cgen),
        .sigFromSrams_bore_135_ram_hold(sigFromSrams_bore_135_ram_hold),
        .sigFromSrams_bore_135_ram_bypass(sigFromSrams_bore_135_ram_bypass),
        .sigFromSrams_bore_135_ram_bp_clken(sigFromSrams_bore_135_ram_bp_clken),
        .sigFromSrams_bore_135_ram_aux_clk(sigFromSrams_bore_135_ram_aux_clk),
        .sigFromSrams_bore_135_ram_aux_ckbp(sigFromSrams_bore_135_ram_aux_ckbp),
        .sigFromSrams_bore_135_ram_mcp_hold(sigFromSrams_bore_135_ram_mcp_hold),
        .sigFromSrams_bore_135_cgen(sigFromSrams_bore_135_cgen),
        .sigFromSrams_bore_136_ram_hold(sigFromSrams_bore_136_ram_hold),
        .sigFromSrams_bore_136_ram_bypass(sigFromSrams_bore_136_ram_bypass),
        .sigFromSrams_bore_136_ram_bp_clken(sigFromSrams_bore_136_ram_bp_clken),
        .sigFromSrams_bore_136_ram_aux_clk(sigFromSrams_bore_136_ram_aux_clk),
        .sigFromSrams_bore_136_ram_aux_ckbp(sigFromSrams_bore_136_ram_aux_ckbp),
        .sigFromSrams_bore_136_ram_mcp_hold(sigFromSrams_bore_136_ram_mcp_hold),
        .sigFromSrams_bore_136_cgen(sigFromSrams_bore_136_cgen),
        .sigFromSrams_bore_137_ram_hold(sigFromSrams_bore_137_ram_hold),
        .sigFromSrams_bore_137_ram_bypass(sigFromSrams_bore_137_ram_bypass),
        .sigFromSrams_bore_137_ram_bp_clken(sigFromSrams_bore_137_ram_bp_clken),
        .sigFromSrams_bore_137_ram_aux_clk(sigFromSrams_bore_137_ram_aux_clk),
        .sigFromSrams_bore_137_ram_aux_ckbp(sigFromSrams_bore_137_ram_aux_ckbp),
        .sigFromSrams_bore_137_ram_mcp_hold(sigFromSrams_bore_137_ram_mcp_hold),
        .sigFromSrams_bore_137_cgen(sigFromSrams_bore_137_cgen),
        .sigFromSrams_bore_138_ram_hold(sigFromSrams_bore_138_ram_hold),
        .sigFromSrams_bore_138_ram_bypass(sigFromSrams_bore_138_ram_bypass),
        .sigFromSrams_bore_138_ram_bp_clken(sigFromSrams_bore_138_ram_bp_clken),
        .sigFromSrams_bore_138_ram_aux_clk(sigFromSrams_bore_138_ram_aux_clk),
        .sigFromSrams_bore_138_ram_aux_ckbp(sigFromSrams_bore_138_ram_aux_ckbp),
        .sigFromSrams_bore_138_ram_mcp_hold(sigFromSrams_bore_138_ram_mcp_hold),
        .sigFromSrams_bore_138_cgen(sigFromSrams_bore_138_cgen),
        .sigFromSrams_bore_139_ram_hold(sigFromSrams_bore_139_ram_hold),
        .sigFromSrams_bore_139_ram_bypass(sigFromSrams_bore_139_ram_bypass),
        .sigFromSrams_bore_139_ram_bp_clken(sigFromSrams_bore_139_ram_bp_clken),
        .sigFromSrams_bore_139_ram_aux_clk(sigFromSrams_bore_139_ram_aux_clk),
        .sigFromSrams_bore_139_ram_aux_ckbp(sigFromSrams_bore_139_ram_aux_ckbp),
        .sigFromSrams_bore_139_ram_mcp_hold(sigFromSrams_bore_139_ram_mcp_hold),
        .sigFromSrams_bore_139_cgen(sigFromSrams_bore_139_cgen),
        .sigFromSrams_bore_140_ram_hold(sigFromSrams_bore_140_ram_hold),
        .sigFromSrams_bore_140_ram_bypass(sigFromSrams_bore_140_ram_bypass),
        .sigFromSrams_bore_140_ram_bp_clken(sigFromSrams_bore_140_ram_bp_clken),
        .sigFromSrams_bore_140_ram_aux_clk(sigFromSrams_bore_140_ram_aux_clk),
        .sigFromSrams_bore_140_ram_aux_ckbp(sigFromSrams_bore_140_ram_aux_ckbp),
        .sigFromSrams_bore_140_ram_mcp_hold(sigFromSrams_bore_140_ram_mcp_hold),
        .sigFromSrams_bore_140_cgen(sigFromSrams_bore_140_cgen),
        .sigFromSrams_bore_141_ram_hold(sigFromSrams_bore_141_ram_hold),
        .sigFromSrams_bore_141_ram_bypass(sigFromSrams_bore_141_ram_bypass),
        .sigFromSrams_bore_141_ram_bp_clken(sigFromSrams_bore_141_ram_bp_clken),
        .sigFromSrams_bore_141_ram_aux_clk(sigFromSrams_bore_141_ram_aux_clk),
        .sigFromSrams_bore_141_ram_aux_ckbp(sigFromSrams_bore_141_ram_aux_ckbp),
        .sigFromSrams_bore_141_ram_mcp_hold(sigFromSrams_bore_141_ram_mcp_hold),
        .sigFromSrams_bore_141_cgen(sigFromSrams_bore_141_cgen),
        .sigFromSrams_bore_142_ram_hold(sigFromSrams_bore_142_ram_hold),
        .sigFromSrams_bore_142_ram_bypass(sigFromSrams_bore_142_ram_bypass),
        .sigFromSrams_bore_142_ram_bp_clken(sigFromSrams_bore_142_ram_bp_clken),
        .sigFromSrams_bore_142_ram_aux_clk(sigFromSrams_bore_142_ram_aux_clk),
        .sigFromSrams_bore_142_ram_aux_ckbp(sigFromSrams_bore_142_ram_aux_ckbp),
        .sigFromSrams_bore_142_ram_mcp_hold(sigFromSrams_bore_142_ram_mcp_hold),
        .sigFromSrams_bore_142_cgen(sigFromSrams_bore_142_cgen),
        .sigFromSrams_bore_143_ram_hold(sigFromSrams_bore_143_ram_hold),
        .sigFromSrams_bore_143_ram_bypass(sigFromSrams_bore_143_ram_bypass),
        .sigFromSrams_bore_143_ram_bp_clken(sigFromSrams_bore_143_ram_bp_clken),
        .sigFromSrams_bore_143_ram_aux_clk(sigFromSrams_bore_143_ram_aux_clk),
        .sigFromSrams_bore_143_ram_aux_ckbp(sigFromSrams_bore_143_ram_aux_ckbp),
        .sigFromSrams_bore_143_ram_mcp_hold(sigFromSrams_bore_143_ram_mcp_hold),
        .sigFromSrams_bore_143_cgen(sigFromSrams_bore_143_cgen)
    );

    assign io_toFtq_prediction_ready_o = io_toFtq_prediction_ready;
    assign s1_fire_o = dut.s1_fire;
    assign abtb_io_stageCtrl_s0_fire_probe_o = dut.abtb_io_stageCtrl_s0_fire_probe;

endmodule
