`timescale 1ns/1ps

module xs_bugcase_tb (
    input  logic        clk,
    input  logic        rst_n,
    output logic [63:0] obs_sig,
    output logic [7551:0] obs_vec
);

    // Deterministic LFSR stimulus (shared between ref/wolf via identical clk/rst).
    logic [255:0] stim;
    wire [511:0] stim_wide = {stim, stim};

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            stim <= 256'h1;
        end else begin
            stim <= {stim[254:0], stim[255] ^ stim[21] ^ stim[1] ^ stim[0]};
        end
    end

    wire io_in_valid = stim_wide[0];
    wire [35:0] io_in_bits_fuType = stim_wide[14 +: 36];
    wire [8:0] io_in_bits_fuOpType = stim_wide[63 +: 9];
    wire [127:0] io_in_bits_src_0 = stim_wide[85 +: 128];
    wire [127:0] io_in_bits_src_1 = stim_wide[226 +: 128];
    wire [127:0] io_in_bits_src_2 = stim_wide[111 +: 128];
    wire [127:0] io_in_bits_src_3 = stim_wide[252 +: 128];
    wire [7:0] io_in_bits_vl = stim_wide[137 +: 8];
    wire io_in_bits_robIdx_flag = stim_wide[158];
    wire [8:0] io_in_bits_robIdx_value = stim_wide[172 +: 9];
    wire [6:0] io_in_bits_pdest = stim_wide[194 +: 7];
    wire io_in_bits_vecWen = stim_wide[214];
    wire io_in_bits_v0Wen = stim_wide[228];
    wire io_in_bits_vlWen = stim_wide[242];
    wire io_in_bits_vpu_vill = stim_wide[0];
    wire io_in_bits_vpu_vma = stim_wide[14];
    wire io_in_bits_vpu_vta = stim_wide[28];
    wire [1:0] io_in_bits_vpu_vsew = stim_wide[42 +: 2];
    wire [2:0] io_in_bits_vpu_vlmul = stim_wide[57 +: 3];
    wire io_in_bits_vpu_specVill = stim_wide[73];
    wire io_in_bits_vpu_specVma = stim_wide[87];
    wire io_in_bits_vpu_specVta = stim_wide[101];
    wire [1:0] io_in_bits_vpu_specVsew = stim_wide[115 +: 2];
    wire [2:0] io_in_bits_vpu_specVlmul = stim_wide[130 +: 3];
    wire io_in_bits_vpu_vm = stim_wide[146];
    wire [7:0] io_in_bits_vpu_vstart = stim_wide[160 +: 8];
    wire [2:0] io_in_bits_vpu_frm = stim_wide[181 +: 3];
    wire io_in_bits_vpu_fpu_isFpToVecInst = stim_wide[197];
    wire io_in_bits_vpu_fpu_isFP32Instr = stim_wide[211];
    wire io_in_bits_vpu_fpu_isFP64Instr = stim_wide[225];
    wire io_in_bits_vpu_fpu_isReduction = stim_wide[239];
    wire io_in_bits_vpu_fpu_isFoldTo1_2 = stim_wide[253];
    wire io_in_bits_vpu_fpu_isFoldTo1_4 = stim_wide[11];
    wire io_in_bits_vpu_fpu_isFoldTo1_8 = stim_wide[25];
    wire [1:0] io_in_bits_vpu_vxrm = stim_wide[39 +: 2];
    wire [6:0] io_in_bits_vpu_vuopIdx = stim_wide[54 +: 7];
    wire io_in_bits_vpu_lastUop = stim_wide[74];
    wire [2:0] io_in_bits_vpu_nf = stim_wide[88 +: 3];
    wire [1:0] io_in_bits_vpu_veew = stim_wide[104 +: 2];
    wire io_in_bits_vpu_isReverse = stim_wide[119];
    wire io_in_bits_vpu_isExt = stim_wide[133];
    wire io_in_bits_vpu_isNarrow = stim_wide[147];
    wire io_in_bits_vpu_isDstMask = stim_wide[161];
    wire io_in_bits_vpu_isOpMask = stim_wide[175];
    wire io_in_bits_vpu_isMove = stim_wide[189];
    wire io_in_bits_vpu_isDependOldVd = stim_wide[203];
    wire io_in_bits_vpu_isWritePartVd = stim_wide[217];
    wire io_in_bits_vpu_isVleff = stim_wide[231];
    wire [15:0] io_in_bits_vpu_maskVecGen = stim_wide[245 +: 16];
    wire io_in_bits_vpu_sew8 = stim_wide[18];
    wire io_in_bits_vpu_sew16 = stim_wide[32];
    wire io_in_bits_vpu_sew32 = stim_wide[46];
    wire io_in_bits_vpu_sew64 = stim_wide[60];
    wire [4:0] io_in_bits_ftqOffset = stim_wide[74 +: 5];
    wire io_in_bits_sqIdx_flag = stim_wide[92];
    wire [5:0] io_in_bits_sqIdx_value = stim_wide[106 +: 6];
    wire io_in_bits_perfDebugInfo_eliminatedMove = stim_wide[125];
    wire [63:0] io_in_bits_perfDebugInfo_renameTime = stim_wide[139 +: 64];
    wire [63:0] io_in_bits_perfDebugInfo_dispatchTime = stim_wide[216 +: 64];
    wire [63:0] io_in_bits_perfDebugInfo_enqRsTime = stim_wide[37 +: 64];
    wire [63:0] io_in_bits_perfDebugInfo_selectTime = stim_wide[114 +: 64];
    wire [63:0] io_in_bits_perfDebugInfo_issueTime = stim_wide[191 +: 64];
    wire [63:0] io_in_bits_perfDebugInfo_runahead_checkpoint_id = stim_wide[12 +: 64];
    wire [63:0] io_in_bits_perfDebugInfo_tlbFirstReqTime = stim_wide[89 +: 64];
    wire [63:0] io_in_bits_perfDebugInfo_tlbRespTime = stim_wide[166 +: 64];
    wire [55:0] io_in_bits_debug_seqNum_seqNum = stim_wide[243 +: 56];
    wire [7:0] io_in_bits_debug_seqNum_uopIdx = stim_wide[56 +: 8];
    wire io_csrCtrl_cache_error_enable = stim_wide[77];
    wire io_rdcache_req_ready = stim_wide[91];
    wire io_rdcache_resp_valid = stim_wide[105];
    wire [127:0] io_rdcache_resp_bits_data_delayed = stim_wide[119 +: 128];
    wire io_rdcache_resp_bits_miss = stim_wide[4];
    wire io_rdcache_resp_bits_tl_error_delayed_tl_denied = stim_wide[18];
    wire io_rdcache_resp_bits_tl_error_delayed_tl_corrupt = stim_wide[32];
    wire io_rdcache_s2_bank_conflict = stim_wide[46];
    wire io_sbuffer_ready = stim_wide[60];
    wire io_dtlb_resp_valid = stim_wide[74];
    wire [47:0] io_dtlb_resp_bits_paddr_0 = stim_wide[88 +: 48];
    wire [63:0] io_dtlb_resp_bits_gpaddr_0 = stim_wide[149 +: 64];
    wire [63:0] io_dtlb_resp_bits_fullva = stim_wide[226 +: 64];
    wire [1:0] io_dtlb_resp_bits_pbmt_0 = stim_wide[47 +: 2];
    wire io_dtlb_resp_bits_miss = stim_wide[62];
    wire io_dtlb_resp_bits_isForVSnonLeafPTE = stim_wide[76];
    wire io_dtlb_resp_bits_excp_0_gpf_ld = stim_wide[90];
    wire io_dtlb_resp_bits_excp_0_gpf_st = stim_wide[104];
    wire io_dtlb_resp_bits_excp_0_pf_ld = stim_wide[118];
    wire io_dtlb_resp_bits_excp_0_pf_st = stim_wide[132];
    wire io_dtlb_resp_bits_excp_0_af_ld = stim_wide[146];
    wire io_dtlb_resp_bits_excp_0_af_st = stim_wide[160];
    wire io_pmpResp_ld = stim_wide[174];
    wire io_pmpResp_st = stim_wide[188];
    wire io_pmpResp_mmio = stim_wide[202];
    wire io_flush_sbuffer_empty = stim_wide[216];
    wire [1:0] io_fromCsrTrigger_tdataVec_0_matchType = stim_wide[230 +: 2];
    wire io_fromCsrTrigger_tdataVec_0_select = stim_wide[245];
    wire io_fromCsrTrigger_tdataVec_0_timing = stim_wide[3];
    wire [3:0] io_fromCsrTrigger_tdataVec_0_action = stim_wide[17 +: 4];
    wire io_fromCsrTrigger_tdataVec_0_chain = stim_wide[34];
    wire io_fromCsrTrigger_tdataVec_0_store = stim_wide[48];
    wire io_fromCsrTrigger_tdataVec_0_load = stim_wide[62];
    wire [63:0] io_fromCsrTrigger_tdataVec_0_tdata2 = stim_wide[76 +: 64];
    wire [1:0] io_fromCsrTrigger_tdataVec_1_matchType = stim_wide[153 +: 2];
    wire io_fromCsrTrigger_tdataVec_1_select = stim_wide[168];
    wire io_fromCsrTrigger_tdataVec_1_timing = stim_wide[182];
    wire [3:0] io_fromCsrTrigger_tdataVec_1_action = stim_wide[196 +: 4];
    wire io_fromCsrTrigger_tdataVec_1_chain = stim_wide[213];
    wire io_fromCsrTrigger_tdataVec_1_store = stim_wide[227];
    wire io_fromCsrTrigger_tdataVec_1_load = stim_wide[241];
    wire [63:0] io_fromCsrTrigger_tdataVec_1_tdata2 = stim_wide[255 +: 64];
    wire [1:0] io_fromCsrTrigger_tdataVec_2_matchType = stim_wide[76 +: 2];
    wire io_fromCsrTrigger_tdataVec_2_select = stim_wide[91];
    wire io_fromCsrTrigger_tdataVec_2_timing = stim_wide[105];
    wire [3:0] io_fromCsrTrigger_tdataVec_2_action = stim_wide[119 +: 4];
    wire io_fromCsrTrigger_tdataVec_2_chain = stim_wide[136];
    wire io_fromCsrTrigger_tdataVec_2_store = stim_wide[150];
    wire io_fromCsrTrigger_tdataVec_2_load = stim_wide[164];
    wire [63:0] io_fromCsrTrigger_tdataVec_2_tdata2 = stim_wide[178 +: 64];
    wire [1:0] io_fromCsrTrigger_tdataVec_3_matchType = stim_wide[255 +: 2];
    wire io_fromCsrTrigger_tdataVec_3_select = stim_wide[14];
    wire io_fromCsrTrigger_tdataVec_3_timing = stim_wide[28];
    wire [3:0] io_fromCsrTrigger_tdataVec_3_action = stim_wide[42 +: 4];
    wire io_fromCsrTrigger_tdataVec_3_chain = stim_wide[59];
    wire io_fromCsrTrigger_tdataVec_3_store = stim_wide[73];
    wire io_fromCsrTrigger_tdataVec_3_load = stim_wide[87];
    wire [63:0] io_fromCsrTrigger_tdataVec_3_tdata2 = stim_wide[101 +: 64];
    wire io_fromCsrTrigger_tEnableVec_0 = stim_wide[178];
    wire io_fromCsrTrigger_tEnableVec_1 = stim_wide[192];
    wire io_fromCsrTrigger_tEnableVec_2 = stim_wide[206];
    wire io_fromCsrTrigger_tEnableVec_3 = stim_wide[220];
    wire io_fromCsrTrigger_debugMode = stim_wide[234];
    wire io_fromCsrTrigger_triggerCanRaiseBpExp = stim_wide[248];

    wire io_uopwriteback_valid;
    wire [127:0] io_uopwriteback_bits_data_0;
    wire [6:0] io_uopwriteback_bits_pdest;
    wire [4:0] io_uopwriteback_bits_pdestVl;
    wire io_uopwriteback_bits_robIdx_flag;
    wire [8:0] io_uopwriteback_bits_robIdx_value;
    wire io_uopwriteback_bits_vecWen;
    wire io_uopwriteback_bits_v0Wen;
    wire io_uopwriteback_bits_vlWen;
    wire io_uopwriteback_bits_exceptionVec_3;
    wire io_uopwriteback_bits_exceptionVec_5;
    wire io_uopwriteback_bits_exceptionVec_7;
    wire io_uopwriteback_bits_exceptionVec_13;
    wire io_uopwriteback_bits_exceptionVec_15;
    wire io_uopwriteback_bits_exceptionVec_19;
    wire io_uopwriteback_bits_exceptionVec_21;
    wire io_uopwriteback_bits_exceptionVec_23;
    wire [3:0] io_uopwriteback_bits_trigger;
    wire io_uopwriteback_bits_vls_vpu_vill;
    wire io_uopwriteback_bits_vls_vpu_vma;
    wire io_uopwriteback_bits_vls_vpu_vta;
    wire [1:0] io_uopwriteback_bits_vls_vpu_vsew;
    wire [2:0] io_uopwriteback_bits_vls_vpu_vlmul;
    wire io_uopwriteback_bits_vls_vpu_specVill;
    wire io_uopwriteback_bits_vls_vpu_specVma;
    wire io_uopwriteback_bits_vls_vpu_specVta;
    wire [1:0] io_uopwriteback_bits_vls_vpu_specVsew;
    wire [2:0] io_uopwriteback_bits_vls_vpu_specVlmul;
    wire io_uopwriteback_bits_vls_vpu_vm;
    wire [7:0] io_uopwriteback_bits_vls_vpu_vstart;
    wire [2:0] io_uopwriteback_bits_vls_vpu_frm;
    wire io_uopwriteback_bits_vls_vpu_fpu_isFpToVecInst;
    wire io_uopwriteback_bits_vls_vpu_fpu_isFP32Instr;
    wire io_uopwriteback_bits_vls_vpu_fpu_isFP64Instr;
    wire io_uopwriteback_bits_vls_vpu_fpu_isReduction;
    wire io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_2;
    wire io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_4;
    wire io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_8;
    wire [1:0] io_uopwriteback_bits_vls_vpu_vxrm;
    wire [6:0] io_uopwriteback_bits_vls_vpu_vuopIdx;
    wire io_uopwriteback_bits_vls_vpu_lastUop;
    wire [127:0] io_uopwriteback_bits_vls_vpu_vmask;
    wire [7:0] io_uopwriteback_bits_vls_vpu_vl;
    wire [2:0] io_uopwriteback_bits_vls_vpu_nf;
    wire [1:0] io_uopwriteback_bits_vls_vpu_veew;
    wire io_uopwriteback_bits_vls_vpu_isReverse;
    wire io_uopwriteback_bits_vls_vpu_isExt;
    wire io_uopwriteback_bits_vls_vpu_isNarrow;
    wire io_uopwriteback_bits_vls_vpu_isDstMask;
    wire io_uopwriteback_bits_vls_vpu_isOpMask;
    wire io_uopwriteback_bits_vls_vpu_isMove;
    wire io_uopwriteback_bits_vls_vpu_isDependOldVd;
    wire io_uopwriteback_bits_vls_vpu_isWritePartVd;
    wire io_uopwriteback_bits_vls_vpu_isVleff;
    wire [15:0] io_uopwriteback_bits_vls_vpu_maskVecGen;
    wire io_uopwriteback_bits_vls_vpu_sew8;
    wire io_uopwriteback_bits_vls_vpu_sew16;
    wire io_uopwriteback_bits_vls_vpu_sew32;
    wire io_uopwriteback_bits_vls_vpu_sew64;
    wire [2:0] io_uopwriteback_bits_vls_vdIdx;
    wire [2:0] io_uopwriteback_bits_vls_vdIdxInField;
    wire io_uopwriteback_bits_vls_isIndexed;
    wire io_uopwriteback_bits_vls_isMasked;
    wire io_uopwriteback_bits_vls_isStrided;
    wire io_uopwriteback_bits_vls_isWhole;
    wire io_uopwriteback_bits_vls_isVecLoad;
    wire io_uopwriteback_bits_vls_isVlm;
    wire io_uopwriteback_bits_debug_isMMIO;
    wire io_uopwriteback_bits_debug_isNCIO;
    wire io_uopwriteback_bits_debug_isPerfCnt;
    wire [47:0] io_uopwriteback_bits_debug_paddr;
    wire [49:0] io_uopwriteback_bits_debug_vaddr;
    wire io_uopwriteback_bits_perfDebugInfo_eliminatedMove;
    wire [63:0] io_uopwriteback_bits_perfDebugInfo_renameTime;
    wire [63:0] io_uopwriteback_bits_perfDebugInfo_dispatchTime;
    wire [63:0] io_uopwriteback_bits_perfDebugInfo_enqRsTime;
    wire [63:0] io_uopwriteback_bits_perfDebugInfo_selectTime;
    wire [63:0] io_uopwriteback_bits_perfDebugInfo_issueTime;
    wire [63:0] io_uopwriteback_bits_perfDebugInfo_runahead_checkpoint_id;
    wire [63:0] io_uopwriteback_bits_perfDebugInfo_tlbFirstReqTime;
    wire [63:0] io_uopwriteback_bits_perfDebugInfo_tlbRespTime;
    wire [55:0] io_uopwriteback_bits_debug_seqNum_seqNum;
    wire [7:0] io_uopwriteback_bits_debug_seqNum_uopIdx;
    wire io_rdcache_req_valid;
    wire [49:0] io_rdcache_req_bits_vaddr;
    wire [49:0] io_rdcache_req_bits_vaddr_dup;
    wire [49:0] io_rdcache_s2_pc;
    wire io_rdcache_is128Req;
    wire [47:0] io_rdcache_s1_paddr_dup_lsu;
    wire [47:0] io_rdcache_s1_paddr_dup_dcache;
    wire io_sbuffer_valid;
    wire [49:0] io_sbuffer_bits_vaddr;
    wire [127:0] io_sbuffer_bits_data;
    wire [15:0] io_sbuffer_bits_mask;
    wire [47:0] io_sbuffer_bits_addr;
    wire io_sbuffer_bits_vecValid;
    wire [35:0] io_vecDifftestInfo_bits_uop_fuType;
    wire [8:0] io_vecDifftestInfo_bits_uop_fuOpType;
    wire [2:0] io_vecDifftestInfo_bits_uop_vpu_nf;
    wire [1:0] io_vecDifftestInfo_bits_uop_vpu_veew;
    wire [8:0] io_vecDifftestInfo_bits_uop_robIdx_value;
    wire io_dtlb_req_valid;
    wire [49:0] io_dtlb_req_bits_vaddr;
    wire [63:0] io_dtlb_req_bits_fullva;
    wire [2:0] io_dtlb_req_bits_cmd;
    wire io_dtlb_req_bits_debug_robIdx_flag;
    wire [8:0] io_dtlb_req_bits_debug_robIdx_value;
    wire io_flush_sbuffer_valid;
    wire io_feedback_valid;
    wire io_feedback_bits_sqIdx_flag;
    wire [5:0] io_feedback_bits_sqIdx_value;
    wire io_exceptionInfo_valid;
    wire [63:0] io_exceptionInfo_bits_vaddr;
    wire [49:0] io_exceptionInfo_bits_gpaddr;
    wire io_exceptionInfo_bits_isForVSnonLeafPTE;

    VSegmentUnit dut (
        .clock(clk),
        .reset(!rst_n),
        .io_in_valid(io_in_valid),
        .io_in_bits_fuType(io_in_bits_fuType),
        .io_in_bits_fuOpType(io_in_bits_fuOpType),
        .io_in_bits_src_0(io_in_bits_src_0),
        .io_in_bits_src_1(io_in_bits_src_1),
        .io_in_bits_src_2(io_in_bits_src_2),
        .io_in_bits_src_3(io_in_bits_src_3),
        .io_in_bits_vl(io_in_bits_vl),
        .io_in_bits_robIdx_flag(io_in_bits_robIdx_flag),
        .io_in_bits_robIdx_value(io_in_bits_robIdx_value),
        .io_in_bits_pdest(io_in_bits_pdest),
        .io_in_bits_vecWen(io_in_bits_vecWen),
        .io_in_bits_v0Wen(io_in_bits_v0Wen),
        .io_in_bits_vlWen(io_in_bits_vlWen),
        .io_in_bits_vpu_vill(io_in_bits_vpu_vill),
        .io_in_bits_vpu_vma(io_in_bits_vpu_vma),
        .io_in_bits_vpu_vta(io_in_bits_vpu_vta),
        .io_in_bits_vpu_vsew(io_in_bits_vpu_vsew),
        .io_in_bits_vpu_vlmul(io_in_bits_vpu_vlmul),
        .io_in_bits_vpu_specVill(io_in_bits_vpu_specVill),
        .io_in_bits_vpu_specVma(io_in_bits_vpu_specVma),
        .io_in_bits_vpu_specVta(io_in_bits_vpu_specVta),
        .io_in_bits_vpu_specVsew(io_in_bits_vpu_specVsew),
        .io_in_bits_vpu_specVlmul(io_in_bits_vpu_specVlmul),
        .io_in_bits_vpu_vm(io_in_bits_vpu_vm),
        .io_in_bits_vpu_vstart(io_in_bits_vpu_vstart),
        .io_in_bits_vpu_frm(io_in_bits_vpu_frm),
        .io_in_bits_vpu_fpu_isFpToVecInst(io_in_bits_vpu_fpu_isFpToVecInst),
        .io_in_bits_vpu_fpu_isFP32Instr(io_in_bits_vpu_fpu_isFP32Instr),
        .io_in_bits_vpu_fpu_isFP64Instr(io_in_bits_vpu_fpu_isFP64Instr),
        .io_in_bits_vpu_fpu_isReduction(io_in_bits_vpu_fpu_isReduction),
        .io_in_bits_vpu_fpu_isFoldTo1_2(io_in_bits_vpu_fpu_isFoldTo1_2),
        .io_in_bits_vpu_fpu_isFoldTo1_4(io_in_bits_vpu_fpu_isFoldTo1_4),
        .io_in_bits_vpu_fpu_isFoldTo1_8(io_in_bits_vpu_fpu_isFoldTo1_8),
        .io_in_bits_vpu_vxrm(io_in_bits_vpu_vxrm),
        .io_in_bits_vpu_vuopIdx(io_in_bits_vpu_vuopIdx),
        .io_in_bits_vpu_lastUop(io_in_bits_vpu_lastUop),
        .io_in_bits_vpu_nf(io_in_bits_vpu_nf),
        .io_in_bits_vpu_veew(io_in_bits_vpu_veew),
        .io_in_bits_vpu_isReverse(io_in_bits_vpu_isReverse),
        .io_in_bits_vpu_isExt(io_in_bits_vpu_isExt),
        .io_in_bits_vpu_isNarrow(io_in_bits_vpu_isNarrow),
        .io_in_bits_vpu_isDstMask(io_in_bits_vpu_isDstMask),
        .io_in_bits_vpu_isOpMask(io_in_bits_vpu_isOpMask),
        .io_in_bits_vpu_isMove(io_in_bits_vpu_isMove),
        .io_in_bits_vpu_isDependOldVd(io_in_bits_vpu_isDependOldVd),
        .io_in_bits_vpu_isWritePartVd(io_in_bits_vpu_isWritePartVd),
        .io_in_bits_vpu_isVleff(io_in_bits_vpu_isVleff),
        .io_in_bits_vpu_maskVecGen(io_in_bits_vpu_maskVecGen),
        .io_in_bits_vpu_sew8(io_in_bits_vpu_sew8),
        .io_in_bits_vpu_sew16(io_in_bits_vpu_sew16),
        .io_in_bits_vpu_sew32(io_in_bits_vpu_sew32),
        .io_in_bits_vpu_sew64(io_in_bits_vpu_sew64),
        .io_in_bits_ftqOffset(io_in_bits_ftqOffset),
        .io_in_bits_sqIdx_flag(io_in_bits_sqIdx_flag),
        .io_in_bits_sqIdx_value(io_in_bits_sqIdx_value),
        .io_in_bits_perfDebugInfo_eliminatedMove(io_in_bits_perfDebugInfo_eliminatedMove),
        .io_in_bits_perfDebugInfo_renameTime(io_in_bits_perfDebugInfo_renameTime),
        .io_in_bits_perfDebugInfo_dispatchTime(io_in_bits_perfDebugInfo_dispatchTime),
        .io_in_bits_perfDebugInfo_enqRsTime(io_in_bits_perfDebugInfo_enqRsTime),
        .io_in_bits_perfDebugInfo_selectTime(io_in_bits_perfDebugInfo_selectTime),
        .io_in_bits_perfDebugInfo_issueTime(io_in_bits_perfDebugInfo_issueTime),
        .io_in_bits_perfDebugInfo_runahead_checkpoint_id(io_in_bits_perfDebugInfo_runahead_checkpoint_id),
        .io_in_bits_perfDebugInfo_tlbFirstReqTime(io_in_bits_perfDebugInfo_tlbFirstReqTime),
        .io_in_bits_perfDebugInfo_tlbRespTime(io_in_bits_perfDebugInfo_tlbRespTime),
        .io_in_bits_debug_seqNum_seqNum(io_in_bits_debug_seqNum_seqNum),
        .io_in_bits_debug_seqNum_uopIdx(io_in_bits_debug_seqNum_uopIdx),
        .io_uopwriteback_valid(io_uopwriteback_valid),
        .io_uopwriteback_bits_data_0(io_uopwriteback_bits_data_0),
        .io_uopwriteback_bits_pdest(io_uopwriteback_bits_pdest),
        .io_uopwriteback_bits_pdestVl(io_uopwriteback_bits_pdestVl),
        .io_uopwriteback_bits_robIdx_flag(io_uopwriteback_bits_robIdx_flag),
        .io_uopwriteback_bits_robIdx_value(io_uopwriteback_bits_robIdx_value),
        .io_uopwriteback_bits_vecWen(io_uopwriteback_bits_vecWen),
        .io_uopwriteback_bits_v0Wen(io_uopwriteback_bits_v0Wen),
        .io_uopwriteback_bits_vlWen(io_uopwriteback_bits_vlWen),
        .io_uopwriteback_bits_exceptionVec_3(io_uopwriteback_bits_exceptionVec_3),
        .io_uopwriteback_bits_exceptionVec_5(io_uopwriteback_bits_exceptionVec_5),
        .io_uopwriteback_bits_exceptionVec_7(io_uopwriteback_bits_exceptionVec_7),
        .io_uopwriteback_bits_exceptionVec_13(io_uopwriteback_bits_exceptionVec_13),
        .io_uopwriteback_bits_exceptionVec_15(io_uopwriteback_bits_exceptionVec_15),
        .io_uopwriteback_bits_exceptionVec_19(io_uopwriteback_bits_exceptionVec_19),
        .io_uopwriteback_bits_exceptionVec_21(io_uopwriteback_bits_exceptionVec_21),
        .io_uopwriteback_bits_exceptionVec_23(io_uopwriteback_bits_exceptionVec_23),
        .io_uopwriteback_bits_trigger(io_uopwriteback_bits_trigger),
        .io_uopwriteback_bits_vls_vpu_vill(io_uopwriteback_bits_vls_vpu_vill),
        .io_uopwriteback_bits_vls_vpu_vma(io_uopwriteback_bits_vls_vpu_vma),
        .io_uopwriteback_bits_vls_vpu_vta(io_uopwriteback_bits_vls_vpu_vta),
        .io_uopwriteback_bits_vls_vpu_vsew(io_uopwriteback_bits_vls_vpu_vsew),
        .io_uopwriteback_bits_vls_vpu_vlmul(io_uopwriteback_bits_vls_vpu_vlmul),
        .io_uopwriteback_bits_vls_vpu_specVill(io_uopwriteback_bits_vls_vpu_specVill),
        .io_uopwriteback_bits_vls_vpu_specVma(io_uopwriteback_bits_vls_vpu_specVma),
        .io_uopwriteback_bits_vls_vpu_specVta(io_uopwriteback_bits_vls_vpu_specVta),
        .io_uopwriteback_bits_vls_vpu_specVsew(io_uopwriteback_bits_vls_vpu_specVsew),
        .io_uopwriteback_bits_vls_vpu_specVlmul(io_uopwriteback_bits_vls_vpu_specVlmul),
        .io_uopwriteback_bits_vls_vpu_vm(io_uopwriteback_bits_vls_vpu_vm),
        .io_uopwriteback_bits_vls_vpu_vstart(io_uopwriteback_bits_vls_vpu_vstart),
        .io_uopwriteback_bits_vls_vpu_frm(io_uopwriteback_bits_vls_vpu_frm),
        .io_uopwriteback_bits_vls_vpu_fpu_isFpToVecInst(io_uopwriteback_bits_vls_vpu_fpu_isFpToVecInst),
        .io_uopwriteback_bits_vls_vpu_fpu_isFP32Instr(io_uopwriteback_bits_vls_vpu_fpu_isFP32Instr),
        .io_uopwriteback_bits_vls_vpu_fpu_isFP64Instr(io_uopwriteback_bits_vls_vpu_fpu_isFP64Instr),
        .io_uopwriteback_bits_vls_vpu_fpu_isReduction(io_uopwriteback_bits_vls_vpu_fpu_isReduction),
        .io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_2(io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_2),
        .io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_4(io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_4),
        .io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_8(io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_8),
        .io_uopwriteback_bits_vls_vpu_vxrm(io_uopwriteback_bits_vls_vpu_vxrm),
        .io_uopwriteback_bits_vls_vpu_vuopIdx(io_uopwriteback_bits_vls_vpu_vuopIdx),
        .io_uopwriteback_bits_vls_vpu_lastUop(io_uopwriteback_bits_vls_vpu_lastUop),
        .io_uopwriteback_bits_vls_vpu_vmask(io_uopwriteback_bits_vls_vpu_vmask),
        .io_uopwriteback_bits_vls_vpu_vl(io_uopwriteback_bits_vls_vpu_vl),
        .io_uopwriteback_bits_vls_vpu_nf(io_uopwriteback_bits_vls_vpu_nf),
        .io_uopwriteback_bits_vls_vpu_veew(io_uopwriteback_bits_vls_vpu_veew),
        .io_uopwriteback_bits_vls_vpu_isReverse(io_uopwriteback_bits_vls_vpu_isReverse),
        .io_uopwriteback_bits_vls_vpu_isExt(io_uopwriteback_bits_vls_vpu_isExt),
        .io_uopwriteback_bits_vls_vpu_isNarrow(io_uopwriteback_bits_vls_vpu_isNarrow),
        .io_uopwriteback_bits_vls_vpu_isDstMask(io_uopwriteback_bits_vls_vpu_isDstMask),
        .io_uopwriteback_bits_vls_vpu_isOpMask(io_uopwriteback_bits_vls_vpu_isOpMask),
        .io_uopwriteback_bits_vls_vpu_isMove(io_uopwriteback_bits_vls_vpu_isMove),
        .io_uopwriteback_bits_vls_vpu_isDependOldVd(io_uopwriteback_bits_vls_vpu_isDependOldVd),
        .io_uopwriteback_bits_vls_vpu_isWritePartVd(io_uopwriteback_bits_vls_vpu_isWritePartVd),
        .io_uopwriteback_bits_vls_vpu_isVleff(io_uopwriteback_bits_vls_vpu_isVleff),
        .io_uopwriteback_bits_vls_vpu_maskVecGen(io_uopwriteback_bits_vls_vpu_maskVecGen),
        .io_uopwriteback_bits_vls_vpu_sew8(io_uopwriteback_bits_vls_vpu_sew8),
        .io_uopwriteback_bits_vls_vpu_sew16(io_uopwriteback_bits_vls_vpu_sew16),
        .io_uopwriteback_bits_vls_vpu_sew32(io_uopwriteback_bits_vls_vpu_sew32),
        .io_uopwriteback_bits_vls_vpu_sew64(io_uopwriteback_bits_vls_vpu_sew64),
        .io_uopwriteback_bits_vls_vdIdx(io_uopwriteback_bits_vls_vdIdx),
        .io_uopwriteback_bits_vls_vdIdxInField(io_uopwriteback_bits_vls_vdIdxInField),
        .io_uopwriteback_bits_vls_isIndexed(io_uopwriteback_bits_vls_isIndexed),
        .io_uopwriteback_bits_vls_isMasked(io_uopwriteback_bits_vls_isMasked),
        .io_uopwriteback_bits_vls_isStrided(io_uopwriteback_bits_vls_isStrided),
        .io_uopwriteback_bits_vls_isWhole(io_uopwriteback_bits_vls_isWhole),
        .io_uopwriteback_bits_vls_isVecLoad(io_uopwriteback_bits_vls_isVecLoad),
        .io_uopwriteback_bits_vls_isVlm(io_uopwriteback_bits_vls_isVlm),
        .io_uopwriteback_bits_debug_isMMIO(io_uopwriteback_bits_debug_isMMIO),
        .io_uopwriteback_bits_debug_isNCIO(io_uopwriteback_bits_debug_isNCIO),
        .io_uopwriteback_bits_debug_isPerfCnt(io_uopwriteback_bits_debug_isPerfCnt),
        .io_uopwriteback_bits_debug_paddr(io_uopwriteback_bits_debug_paddr),
        .io_uopwriteback_bits_debug_vaddr(io_uopwriteback_bits_debug_vaddr),
        .io_uopwriteback_bits_perfDebugInfo_eliminatedMove(io_uopwriteback_bits_perfDebugInfo_eliminatedMove),
        .io_uopwriteback_bits_perfDebugInfo_renameTime(io_uopwriteback_bits_perfDebugInfo_renameTime),
        .io_uopwriteback_bits_perfDebugInfo_dispatchTime(io_uopwriteback_bits_perfDebugInfo_dispatchTime),
        .io_uopwriteback_bits_perfDebugInfo_enqRsTime(io_uopwriteback_bits_perfDebugInfo_enqRsTime),
        .io_uopwriteback_bits_perfDebugInfo_selectTime(io_uopwriteback_bits_perfDebugInfo_selectTime),
        .io_uopwriteback_bits_perfDebugInfo_issueTime(io_uopwriteback_bits_perfDebugInfo_issueTime),
        .io_uopwriteback_bits_perfDebugInfo_runahead_checkpoint_id(io_uopwriteback_bits_perfDebugInfo_runahead_checkpoint_id),
        .io_uopwriteback_bits_perfDebugInfo_tlbFirstReqTime(io_uopwriteback_bits_perfDebugInfo_tlbFirstReqTime),
        .io_uopwriteback_bits_perfDebugInfo_tlbRespTime(io_uopwriteback_bits_perfDebugInfo_tlbRespTime),
        .io_uopwriteback_bits_debug_seqNum_seqNum(io_uopwriteback_bits_debug_seqNum_seqNum),
        .io_uopwriteback_bits_debug_seqNum_uopIdx(io_uopwriteback_bits_debug_seqNum_uopIdx),
        .io_csrCtrl_cache_error_enable(io_csrCtrl_cache_error_enable),
        .io_rdcache_req_ready(io_rdcache_req_ready),
        .io_rdcache_req_valid(io_rdcache_req_valid),
        .io_rdcache_req_bits_vaddr(io_rdcache_req_bits_vaddr),
        .io_rdcache_req_bits_vaddr_dup(io_rdcache_req_bits_vaddr_dup),
        .io_rdcache_resp_valid(io_rdcache_resp_valid),
        .io_rdcache_resp_bits_data_delayed(io_rdcache_resp_bits_data_delayed),
        .io_rdcache_resp_bits_miss(io_rdcache_resp_bits_miss),
        .io_rdcache_resp_bits_tl_error_delayed_tl_denied(io_rdcache_resp_bits_tl_error_delayed_tl_denied),
        .io_rdcache_resp_bits_tl_error_delayed_tl_corrupt(io_rdcache_resp_bits_tl_error_delayed_tl_corrupt),
        .io_rdcache_s2_pc(io_rdcache_s2_pc),
        .io_rdcache_is128Req(io_rdcache_is128Req),
        .io_rdcache_s1_paddr_dup_lsu(io_rdcache_s1_paddr_dup_lsu),
        .io_rdcache_s1_paddr_dup_dcache(io_rdcache_s1_paddr_dup_dcache),
        .io_rdcache_s2_bank_conflict(io_rdcache_s2_bank_conflict),
        .io_sbuffer_ready(io_sbuffer_ready),
        .io_sbuffer_valid(io_sbuffer_valid),
        .io_sbuffer_bits_vaddr(io_sbuffer_bits_vaddr),
        .io_sbuffer_bits_data(io_sbuffer_bits_data),
        .io_sbuffer_bits_mask(io_sbuffer_bits_mask),
        .io_sbuffer_bits_addr(io_sbuffer_bits_addr),
        .io_sbuffer_bits_vecValid(io_sbuffer_bits_vecValid),
        .io_vecDifftestInfo_bits_uop_fuType(io_vecDifftestInfo_bits_uop_fuType),
        .io_vecDifftestInfo_bits_uop_fuOpType(io_vecDifftestInfo_bits_uop_fuOpType),
        .io_vecDifftestInfo_bits_uop_vpu_nf(io_vecDifftestInfo_bits_uop_vpu_nf),
        .io_vecDifftestInfo_bits_uop_vpu_veew(io_vecDifftestInfo_bits_uop_vpu_veew),
        .io_vecDifftestInfo_bits_uop_robIdx_value(io_vecDifftestInfo_bits_uop_robIdx_value),
        .io_dtlb_req_valid(io_dtlb_req_valid),
        .io_dtlb_req_bits_vaddr(io_dtlb_req_bits_vaddr),
        .io_dtlb_req_bits_fullva(io_dtlb_req_bits_fullva),
        .io_dtlb_req_bits_cmd(io_dtlb_req_bits_cmd),
        .io_dtlb_req_bits_debug_robIdx_flag(io_dtlb_req_bits_debug_robIdx_flag),
        .io_dtlb_req_bits_debug_robIdx_value(io_dtlb_req_bits_debug_robIdx_value),
        .io_dtlb_resp_valid(io_dtlb_resp_valid),
        .io_dtlb_resp_bits_paddr_0(io_dtlb_resp_bits_paddr_0),
        .io_dtlb_resp_bits_gpaddr_0(io_dtlb_resp_bits_gpaddr_0),
        .io_dtlb_resp_bits_fullva(io_dtlb_resp_bits_fullva),
        .io_dtlb_resp_bits_pbmt_0(io_dtlb_resp_bits_pbmt_0),
        .io_dtlb_resp_bits_miss(io_dtlb_resp_bits_miss),
        .io_dtlb_resp_bits_isForVSnonLeafPTE(io_dtlb_resp_bits_isForVSnonLeafPTE),
        .io_dtlb_resp_bits_excp_0_gpf_ld(io_dtlb_resp_bits_excp_0_gpf_ld),
        .io_dtlb_resp_bits_excp_0_gpf_st(io_dtlb_resp_bits_excp_0_gpf_st),
        .io_dtlb_resp_bits_excp_0_pf_ld(io_dtlb_resp_bits_excp_0_pf_ld),
        .io_dtlb_resp_bits_excp_0_pf_st(io_dtlb_resp_bits_excp_0_pf_st),
        .io_dtlb_resp_bits_excp_0_af_ld(io_dtlb_resp_bits_excp_0_af_ld),
        .io_dtlb_resp_bits_excp_0_af_st(io_dtlb_resp_bits_excp_0_af_st),
        .io_pmpResp_ld(io_pmpResp_ld),
        .io_pmpResp_st(io_pmpResp_st),
        .io_pmpResp_mmio(io_pmpResp_mmio),
        .io_flush_sbuffer_valid(io_flush_sbuffer_valid),
        .io_flush_sbuffer_empty(io_flush_sbuffer_empty),
        .io_feedback_valid(io_feedback_valid),
        .io_feedback_bits_sqIdx_flag(io_feedback_bits_sqIdx_flag),
        .io_feedback_bits_sqIdx_value(io_feedback_bits_sqIdx_value),
        .io_exceptionInfo_valid(io_exceptionInfo_valid),
        .io_exceptionInfo_bits_vaddr(io_exceptionInfo_bits_vaddr),
        .io_exceptionInfo_bits_gpaddr(io_exceptionInfo_bits_gpaddr),
        .io_exceptionInfo_bits_isForVSnonLeafPTE(io_exceptionInfo_bits_isForVSnonLeafPTE),
        .io_fromCsrTrigger_tdataVec_0_matchType(io_fromCsrTrigger_tdataVec_0_matchType),
        .io_fromCsrTrigger_tdataVec_0_select(io_fromCsrTrigger_tdataVec_0_select),
        .io_fromCsrTrigger_tdataVec_0_timing(io_fromCsrTrigger_tdataVec_0_timing),
        .io_fromCsrTrigger_tdataVec_0_action(io_fromCsrTrigger_tdataVec_0_action),
        .io_fromCsrTrigger_tdataVec_0_chain(io_fromCsrTrigger_tdataVec_0_chain),
        .io_fromCsrTrigger_tdataVec_0_store(io_fromCsrTrigger_tdataVec_0_store),
        .io_fromCsrTrigger_tdataVec_0_load(io_fromCsrTrigger_tdataVec_0_load),
        .io_fromCsrTrigger_tdataVec_0_tdata2(io_fromCsrTrigger_tdataVec_0_tdata2),
        .io_fromCsrTrigger_tdataVec_1_matchType(io_fromCsrTrigger_tdataVec_1_matchType),
        .io_fromCsrTrigger_tdataVec_1_select(io_fromCsrTrigger_tdataVec_1_select),
        .io_fromCsrTrigger_tdataVec_1_timing(io_fromCsrTrigger_tdataVec_1_timing),
        .io_fromCsrTrigger_tdataVec_1_action(io_fromCsrTrigger_tdataVec_1_action),
        .io_fromCsrTrigger_tdataVec_1_chain(io_fromCsrTrigger_tdataVec_1_chain),
        .io_fromCsrTrigger_tdataVec_1_store(io_fromCsrTrigger_tdataVec_1_store),
        .io_fromCsrTrigger_tdataVec_1_load(io_fromCsrTrigger_tdataVec_1_load),
        .io_fromCsrTrigger_tdataVec_1_tdata2(io_fromCsrTrigger_tdataVec_1_tdata2),
        .io_fromCsrTrigger_tdataVec_2_matchType(io_fromCsrTrigger_tdataVec_2_matchType),
        .io_fromCsrTrigger_tdataVec_2_select(io_fromCsrTrigger_tdataVec_2_select),
        .io_fromCsrTrigger_tdataVec_2_timing(io_fromCsrTrigger_tdataVec_2_timing),
        .io_fromCsrTrigger_tdataVec_2_action(io_fromCsrTrigger_tdataVec_2_action),
        .io_fromCsrTrigger_tdataVec_2_chain(io_fromCsrTrigger_tdataVec_2_chain),
        .io_fromCsrTrigger_tdataVec_2_store(io_fromCsrTrigger_tdataVec_2_store),
        .io_fromCsrTrigger_tdataVec_2_load(io_fromCsrTrigger_tdataVec_2_load),
        .io_fromCsrTrigger_tdataVec_2_tdata2(io_fromCsrTrigger_tdataVec_2_tdata2),
        .io_fromCsrTrigger_tdataVec_3_matchType(io_fromCsrTrigger_tdataVec_3_matchType),
        .io_fromCsrTrigger_tdataVec_3_select(io_fromCsrTrigger_tdataVec_3_select),
        .io_fromCsrTrigger_tdataVec_3_timing(io_fromCsrTrigger_tdataVec_3_timing),
        .io_fromCsrTrigger_tdataVec_3_action(io_fromCsrTrigger_tdataVec_3_action),
        .io_fromCsrTrigger_tdataVec_3_chain(io_fromCsrTrigger_tdataVec_3_chain),
        .io_fromCsrTrigger_tdataVec_3_store(io_fromCsrTrigger_tdataVec_3_store),
        .io_fromCsrTrigger_tdataVec_3_load(io_fromCsrTrigger_tdataVec_3_load),
        .io_fromCsrTrigger_tdataVec_3_tdata2(io_fromCsrTrigger_tdataVec_3_tdata2),
        .io_fromCsrTrigger_tEnableVec_0(io_fromCsrTrigger_tEnableVec_0),
        .io_fromCsrTrigger_tEnableVec_1(io_fromCsrTrigger_tEnableVec_1),
        .io_fromCsrTrigger_tEnableVec_2(io_fromCsrTrigger_tEnableVec_2),
        .io_fromCsrTrigger_tEnableVec_3(io_fromCsrTrigger_tEnableVec_3),
        .io_fromCsrTrigger_debugMode(io_fromCsrTrigger_debugMode),
        .io_fromCsrTrigger_triggerCanRaiseBpExp(io_fromCsrTrigger_triggerCanRaiseBpExp)
    );

    wire [3:0] dbg_state = dut.state;
    wire [3:0] dbg_stateNext = dut.stateNext;
    wire dbg_flush_sbuffer_empty = io_flush_sbuffer_empty;

    wire [63:0] sig_io_uopwriteback_valid = {63'b0, io_uopwriteback_valid};
    wire [63:0] sig_io_uopwriteback_bits_data_0 = io_uopwriteback_bits_data_0[63:0] ^ io_uopwriteback_bits_data_0[127:64];
    wire [63:0] sig_io_uopwriteback_bits_pdest = {57'b0, io_uopwriteback_bits_pdest};
    wire [63:0] sig_io_uopwriteback_bits_pdestVl = {59'b0, io_uopwriteback_bits_pdestVl};
    wire [63:0] sig_io_uopwriteback_bits_robIdx_flag = {63'b0, io_uopwriteback_bits_robIdx_flag};
    wire [63:0] sig_io_uopwriteback_bits_robIdx_value = {55'b0, io_uopwriteback_bits_robIdx_value};
    wire [63:0] sig_io_uopwriteback_bits_vecWen = {63'b0, io_uopwriteback_bits_vecWen};
    wire [63:0] sig_io_uopwriteback_bits_v0Wen = {63'b0, io_uopwriteback_bits_v0Wen};
    wire [63:0] sig_io_uopwriteback_bits_vlWen = {63'b0, io_uopwriteback_bits_vlWen};
    wire [63:0] sig_io_uopwriteback_bits_exceptionVec_3 = {63'b0, io_uopwriteback_bits_exceptionVec_3};
    wire [63:0] sig_io_uopwriteback_bits_exceptionVec_5 = {63'b0, io_uopwriteback_bits_exceptionVec_5};
    wire [63:0] sig_io_uopwriteback_bits_exceptionVec_7 = {63'b0, io_uopwriteback_bits_exceptionVec_7};
    wire [63:0] sig_io_uopwriteback_bits_exceptionVec_13 = {63'b0, io_uopwriteback_bits_exceptionVec_13};
    wire [63:0] sig_io_uopwriteback_bits_exceptionVec_15 = {63'b0, io_uopwriteback_bits_exceptionVec_15};
    wire [63:0] sig_io_uopwriteback_bits_exceptionVec_19 = {63'b0, io_uopwriteback_bits_exceptionVec_19};
    wire [63:0] sig_io_uopwriteback_bits_exceptionVec_21 = {63'b0, io_uopwriteback_bits_exceptionVec_21};
    wire [63:0] sig_io_uopwriteback_bits_exceptionVec_23 = {63'b0, io_uopwriteback_bits_exceptionVec_23};
    wire [63:0] sig_io_uopwriteback_bits_trigger = {60'b0, io_uopwriteback_bits_trigger};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vill = {63'b0, io_uopwriteback_bits_vls_vpu_vill};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vma = {63'b0, io_uopwriteback_bits_vls_vpu_vma};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vta = {63'b0, io_uopwriteback_bits_vls_vpu_vta};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vsew = {62'b0, io_uopwriteback_bits_vls_vpu_vsew};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vlmul = {61'b0, io_uopwriteback_bits_vls_vpu_vlmul};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_specVill = {63'b0, io_uopwriteback_bits_vls_vpu_specVill};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_specVma = {63'b0, io_uopwriteback_bits_vls_vpu_specVma};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_specVta = {63'b0, io_uopwriteback_bits_vls_vpu_specVta};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_specVsew = {62'b0, io_uopwriteback_bits_vls_vpu_specVsew};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_specVlmul = {61'b0, io_uopwriteback_bits_vls_vpu_specVlmul};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vm = {63'b0, io_uopwriteback_bits_vls_vpu_vm};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vstart = {56'b0, io_uopwriteback_bits_vls_vpu_vstart};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_frm = {61'b0, io_uopwriteback_bits_vls_vpu_frm};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_fpu_isFpToVecInst = {63'b0, io_uopwriteback_bits_vls_vpu_fpu_isFpToVecInst};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_fpu_isFP32Instr = {63'b0, io_uopwriteback_bits_vls_vpu_fpu_isFP32Instr};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_fpu_isFP64Instr = {63'b0, io_uopwriteback_bits_vls_vpu_fpu_isFP64Instr};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_fpu_isReduction = {63'b0, io_uopwriteback_bits_vls_vpu_fpu_isReduction};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_2 = {63'b0, io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_2};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_4 = {63'b0, io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_4};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_8 = {63'b0, io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_8};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vxrm = {62'b0, io_uopwriteback_bits_vls_vpu_vxrm};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vuopIdx = {57'b0, io_uopwriteback_bits_vls_vpu_vuopIdx};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_lastUop = {63'b0, io_uopwriteback_bits_vls_vpu_lastUop};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vmask = io_uopwriteback_bits_vls_vpu_vmask[63:0] ^ io_uopwriteback_bits_vls_vpu_vmask[127:64];
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_vl = {56'b0, io_uopwriteback_bits_vls_vpu_vl};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_nf = {61'b0, io_uopwriteback_bits_vls_vpu_nf};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_veew = {62'b0, io_uopwriteback_bits_vls_vpu_veew};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_isReverse = {63'b0, io_uopwriteback_bits_vls_vpu_isReverse};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_isExt = {63'b0, io_uopwriteback_bits_vls_vpu_isExt};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_isNarrow = {63'b0, io_uopwriteback_bits_vls_vpu_isNarrow};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_isDstMask = {63'b0, io_uopwriteback_bits_vls_vpu_isDstMask};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_isOpMask = {63'b0, io_uopwriteback_bits_vls_vpu_isOpMask};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_isMove = {63'b0, io_uopwriteback_bits_vls_vpu_isMove};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_isDependOldVd = {63'b0, io_uopwriteback_bits_vls_vpu_isDependOldVd};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_isWritePartVd = {63'b0, io_uopwriteback_bits_vls_vpu_isWritePartVd};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_isVleff = {63'b0, io_uopwriteback_bits_vls_vpu_isVleff};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_maskVecGen = {48'b0, io_uopwriteback_bits_vls_vpu_maskVecGen};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_sew8 = {63'b0, io_uopwriteback_bits_vls_vpu_sew8};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_sew16 = {63'b0, io_uopwriteback_bits_vls_vpu_sew16};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_sew32 = {63'b0, io_uopwriteback_bits_vls_vpu_sew32};
    wire [63:0] sig_io_uopwriteback_bits_vls_vpu_sew64 = {63'b0, io_uopwriteback_bits_vls_vpu_sew64};
    wire [63:0] sig_io_uopwriteback_bits_vls_vdIdx = {61'b0, io_uopwriteback_bits_vls_vdIdx};
    wire [63:0] sig_io_uopwriteback_bits_vls_vdIdxInField = {61'b0, io_uopwriteback_bits_vls_vdIdxInField};
    wire [63:0] sig_io_uopwriteback_bits_vls_isIndexed = {63'b0, io_uopwriteback_bits_vls_isIndexed};
    wire [63:0] sig_io_uopwriteback_bits_vls_isMasked = {63'b0, io_uopwriteback_bits_vls_isMasked};
    wire [63:0] sig_io_uopwriteback_bits_vls_isStrided = {63'b0, io_uopwriteback_bits_vls_isStrided};
    wire [63:0] sig_io_uopwriteback_bits_vls_isWhole = {63'b0, io_uopwriteback_bits_vls_isWhole};
    wire [63:0] sig_io_uopwriteback_bits_vls_isVecLoad = {63'b0, io_uopwriteback_bits_vls_isVecLoad};
    wire [63:0] sig_io_uopwriteback_bits_vls_isVlm = {63'b0, io_uopwriteback_bits_vls_isVlm};
    wire [63:0] sig_io_uopwriteback_bits_debug_isMMIO = {63'b0, io_uopwriteback_bits_debug_isMMIO};
    wire [63:0] sig_io_uopwriteback_bits_debug_isNCIO = {63'b0, io_uopwriteback_bits_debug_isNCIO};
    wire [63:0] sig_io_uopwriteback_bits_debug_isPerfCnt = {63'b0, io_uopwriteback_bits_debug_isPerfCnt};
    wire [63:0] sig_io_uopwriteback_bits_debug_paddr = {16'b0, io_uopwriteback_bits_debug_paddr};
    wire [63:0] sig_io_uopwriteback_bits_debug_vaddr = {14'b0, io_uopwriteback_bits_debug_vaddr};
    wire [63:0] sig_io_uopwriteback_bits_perfDebugInfo_eliminatedMove = {63'b0, io_uopwriteback_bits_perfDebugInfo_eliminatedMove};
    wire [63:0] sig_io_uopwriteback_bits_perfDebugInfo_renameTime = io_uopwriteback_bits_perfDebugInfo_renameTime;
    wire [63:0] sig_io_uopwriteback_bits_perfDebugInfo_dispatchTime = io_uopwriteback_bits_perfDebugInfo_dispatchTime;
    wire [63:0] sig_io_uopwriteback_bits_perfDebugInfo_enqRsTime = io_uopwriteback_bits_perfDebugInfo_enqRsTime;
    wire [63:0] sig_io_uopwriteback_bits_perfDebugInfo_selectTime = io_uopwriteback_bits_perfDebugInfo_selectTime;
    wire [63:0] sig_io_uopwriteback_bits_perfDebugInfo_issueTime = io_uopwriteback_bits_perfDebugInfo_issueTime;
    wire [63:0] sig_io_uopwriteback_bits_perfDebugInfo_runahead_checkpoint_id = io_uopwriteback_bits_perfDebugInfo_runahead_checkpoint_id;
    wire [63:0] sig_io_uopwriteback_bits_perfDebugInfo_tlbFirstReqTime = io_uopwriteback_bits_perfDebugInfo_tlbFirstReqTime;
    wire [63:0] sig_io_uopwriteback_bits_perfDebugInfo_tlbRespTime = io_uopwriteback_bits_perfDebugInfo_tlbRespTime;
    wire [63:0] sig_io_uopwriteback_bits_debug_seqNum_seqNum = {8'b0, io_uopwriteback_bits_debug_seqNum_seqNum};
    wire [63:0] sig_io_uopwriteback_bits_debug_seqNum_uopIdx = {56'b0, io_uopwriteback_bits_debug_seqNum_uopIdx};
    wire [63:0] sig_io_rdcache_req_valid = {63'b0, io_rdcache_req_valid};
    wire [63:0] sig_io_rdcache_req_bits_vaddr = {14'b0, io_rdcache_req_bits_vaddr};
    wire [63:0] sig_io_rdcache_req_bits_vaddr_dup = {14'b0, io_rdcache_req_bits_vaddr_dup};
    wire [63:0] sig_io_rdcache_s2_pc = {14'b0, io_rdcache_s2_pc};
    wire [63:0] sig_io_rdcache_is128Req = {63'b0, io_rdcache_is128Req};
    wire [63:0] sig_io_rdcache_s1_paddr_dup_lsu = {16'b0, io_rdcache_s1_paddr_dup_lsu};
    wire [63:0] sig_io_rdcache_s1_paddr_dup_dcache = {16'b0, io_rdcache_s1_paddr_dup_dcache};
    wire [63:0] sig_io_sbuffer_valid = {63'b0, io_sbuffer_valid};
    wire [63:0] sig_io_sbuffer_bits_vaddr = {14'b0, io_sbuffer_bits_vaddr};
    wire [63:0] sig_io_sbuffer_bits_data = io_sbuffer_bits_data[63:0] ^ io_sbuffer_bits_data[127:64];
    wire [63:0] sig_io_sbuffer_bits_mask = {48'b0, io_sbuffer_bits_mask};
    wire [63:0] sig_io_sbuffer_bits_addr = {16'b0, io_sbuffer_bits_addr};
    wire [63:0] sig_io_sbuffer_bits_vecValid = {63'b0, io_sbuffer_bits_vecValid};
    wire [63:0] sig_io_vecDifftestInfo_bits_uop_fuType = {28'b0, io_vecDifftestInfo_bits_uop_fuType};
    wire [63:0] sig_io_vecDifftestInfo_bits_uop_fuOpType = {55'b0, io_vecDifftestInfo_bits_uop_fuOpType};
    wire [63:0] sig_io_vecDifftestInfo_bits_uop_vpu_nf = {61'b0, io_vecDifftestInfo_bits_uop_vpu_nf};
    wire [63:0] sig_io_vecDifftestInfo_bits_uop_vpu_veew = {62'b0, io_vecDifftestInfo_bits_uop_vpu_veew};
    wire [63:0] sig_io_vecDifftestInfo_bits_uop_robIdx_value = {55'b0, io_vecDifftestInfo_bits_uop_robIdx_value};
    wire [63:0] sig_io_dtlb_req_valid = {63'b0, io_dtlb_req_valid};
    wire [63:0] sig_io_dtlb_req_bits_vaddr = {14'b0, io_dtlb_req_bits_vaddr};
    wire [63:0] sig_io_dtlb_req_bits_fullva = io_dtlb_req_bits_fullva;
    wire [63:0] sig_io_dtlb_req_bits_cmd = {61'b0, io_dtlb_req_bits_cmd};
    wire [63:0] sig_io_dtlb_req_bits_debug_robIdx_flag = {63'b0, io_dtlb_req_bits_debug_robIdx_flag};
    wire [63:0] sig_io_dtlb_req_bits_debug_robIdx_value = {55'b0, io_dtlb_req_bits_debug_robIdx_value};
    wire [63:0] sig_io_flush_sbuffer_valid = {63'b0, io_flush_sbuffer_valid};
    wire [63:0] sig_io_feedback_valid = {63'b0, io_feedback_valid};
    wire [63:0] sig_io_feedback_bits_sqIdx_flag = {63'b0, io_feedback_bits_sqIdx_flag};
    wire [63:0] sig_io_feedback_bits_sqIdx_value = {58'b0, io_feedback_bits_sqIdx_value};
    wire [63:0] sig_io_exceptionInfo_valid = {63'b0, io_exceptionInfo_valid};
    wire [63:0] sig_io_exceptionInfo_bits_vaddr = io_exceptionInfo_bits_vaddr;
    wire [63:0] sig_io_exceptionInfo_bits_gpaddr = {14'b0, io_exceptionInfo_bits_gpaddr};
    wire [63:0] sig_io_exceptionInfo_bits_isForVSnonLeafPTE = {63'b0, io_exceptionInfo_bits_isForVSnonLeafPTE};
    wire [63:0] sig_dbg_state = {60'b0, dbg_state};
    wire [63:0] sig_dbg_stateNext = {60'b0, dbg_stateNext};
    wire [63:0] sig_io_flush_sbuffer_empty = {63'b0, dbg_flush_sbuffer_empty};

    always_comb begin
        obs_sig = 64'h0;
        obs_sig = obs_sig ^ sig_io_uopwriteback_valid;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_data_0;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_pdest;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_pdestVl;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_robIdx_flag;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_robIdx_value;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vecWen;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_v0Wen;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vlWen;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_exceptionVec_3;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_exceptionVec_5;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_exceptionVec_7;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_exceptionVec_13;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_exceptionVec_15;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_exceptionVec_19;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_exceptionVec_21;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_exceptionVec_23;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_trigger;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vill;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vma;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vta;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vsew;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vlmul;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_specVill;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_specVma;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_specVta;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_specVsew;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_specVlmul;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vm;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vstart;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_frm;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_fpu_isFpToVecInst;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_fpu_isFP32Instr;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_fpu_isFP64Instr;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_fpu_isReduction;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_2;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_4;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_8;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vxrm;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vuopIdx;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_lastUop;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vmask;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_vl;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_nf;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_veew;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_isReverse;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_isExt;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_isNarrow;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_isDstMask;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_isOpMask;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_isMove;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_isDependOldVd;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_isWritePartVd;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_isVleff;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_maskVecGen;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_sew8;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_sew16;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_sew32;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vpu_sew64;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vdIdx;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_vdIdxInField;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_isIndexed;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_isMasked;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_isStrided;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_isWhole;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_isVecLoad;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_vls_isVlm;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_debug_isMMIO;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_debug_isNCIO;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_debug_isPerfCnt;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_debug_paddr;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_debug_vaddr;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_perfDebugInfo_eliminatedMove;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_perfDebugInfo_renameTime;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_perfDebugInfo_dispatchTime;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_perfDebugInfo_enqRsTime;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_perfDebugInfo_selectTime;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_perfDebugInfo_issueTime;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_perfDebugInfo_runahead_checkpoint_id;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_perfDebugInfo_tlbFirstReqTime;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_perfDebugInfo_tlbRespTime;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_debug_seqNum_seqNum;
        obs_sig = obs_sig ^ sig_io_uopwriteback_bits_debug_seqNum_uopIdx;
        obs_sig = obs_sig ^ sig_io_rdcache_req_valid;
        obs_sig = obs_sig ^ sig_io_rdcache_req_bits_vaddr;
        obs_sig = obs_sig ^ sig_io_rdcache_req_bits_vaddr_dup;
        obs_sig = obs_sig ^ sig_io_rdcache_s2_pc;
        obs_sig = obs_sig ^ sig_io_rdcache_is128Req;
        obs_sig = obs_sig ^ sig_io_rdcache_s1_paddr_dup_lsu;
        obs_sig = obs_sig ^ sig_io_rdcache_s1_paddr_dup_dcache;
        obs_sig = obs_sig ^ sig_io_sbuffer_valid;
        obs_sig = obs_sig ^ sig_io_sbuffer_bits_vaddr;
        obs_sig = obs_sig ^ sig_io_sbuffer_bits_data;
        obs_sig = obs_sig ^ sig_io_sbuffer_bits_mask;
        obs_sig = obs_sig ^ sig_io_sbuffer_bits_addr;
        obs_sig = obs_sig ^ sig_io_sbuffer_bits_vecValid;
        obs_sig = obs_sig ^ sig_io_vecDifftestInfo_bits_uop_fuType;
        obs_sig = obs_sig ^ sig_io_vecDifftestInfo_bits_uop_fuOpType;
        obs_sig = obs_sig ^ sig_io_vecDifftestInfo_bits_uop_vpu_nf;
        obs_sig = obs_sig ^ sig_io_vecDifftestInfo_bits_uop_vpu_veew;
        obs_sig = obs_sig ^ sig_io_vecDifftestInfo_bits_uop_robIdx_value;
        obs_sig = obs_sig ^ sig_io_dtlb_req_valid;
        obs_sig = obs_sig ^ sig_io_dtlb_req_bits_vaddr;
        obs_sig = obs_sig ^ sig_io_dtlb_req_bits_fullva;
        obs_sig = obs_sig ^ sig_io_dtlb_req_bits_cmd;
        obs_sig = obs_sig ^ sig_io_dtlb_req_bits_debug_robIdx_flag;
        obs_sig = obs_sig ^ sig_io_dtlb_req_bits_debug_robIdx_value;
        obs_sig = obs_sig ^ sig_io_flush_sbuffer_valid;
        obs_sig = obs_sig ^ sig_io_feedback_valid;
        obs_sig = obs_sig ^ sig_io_feedback_bits_sqIdx_flag;
        obs_sig = obs_sig ^ sig_io_feedback_bits_sqIdx_value;
        obs_sig = obs_sig ^ sig_io_exceptionInfo_valid;
        obs_sig = obs_sig ^ sig_io_exceptionInfo_bits_vaddr;
        obs_sig = obs_sig ^ sig_io_exceptionInfo_bits_gpaddr;
        obs_sig = obs_sig ^ sig_io_exceptionInfo_bits_isForVSnonLeafPTE;
        obs_sig = obs_sig ^ sig_dbg_state;
        obs_sig = obs_sig ^ sig_dbg_stateNext;
        obs_sig = obs_sig ^ sig_io_flush_sbuffer_empty;
    end

    assign obs_vec = {sig_io_flush_sbuffer_empty, sig_dbg_stateNext, sig_dbg_state, sig_io_exceptionInfo_bits_isForVSnonLeafPTE, sig_io_exceptionInfo_bits_gpaddr, sig_io_exceptionInfo_bits_vaddr, sig_io_exceptionInfo_valid, sig_io_feedback_bits_sqIdx_value, sig_io_feedback_bits_sqIdx_flag, sig_io_feedback_valid, sig_io_flush_sbuffer_valid, sig_io_dtlb_req_bits_debug_robIdx_value, sig_io_dtlb_req_bits_debug_robIdx_flag, sig_io_dtlb_req_bits_cmd, sig_io_dtlb_req_bits_fullva, sig_io_dtlb_req_bits_vaddr, sig_io_dtlb_req_valid, sig_io_vecDifftestInfo_bits_uop_robIdx_value, sig_io_vecDifftestInfo_bits_uop_vpu_veew, sig_io_vecDifftestInfo_bits_uop_vpu_nf, sig_io_vecDifftestInfo_bits_uop_fuOpType, sig_io_vecDifftestInfo_bits_uop_fuType, sig_io_sbuffer_bits_vecValid, sig_io_sbuffer_bits_addr, sig_io_sbuffer_bits_mask, sig_io_sbuffer_bits_data, sig_io_sbuffer_bits_vaddr, sig_io_sbuffer_valid, sig_io_rdcache_s1_paddr_dup_dcache, sig_io_rdcache_s1_paddr_dup_lsu, sig_io_rdcache_is128Req, sig_io_rdcache_s2_pc, sig_io_rdcache_req_bits_vaddr_dup, sig_io_rdcache_req_bits_vaddr, sig_io_rdcache_req_valid, sig_io_uopwriteback_bits_debug_seqNum_uopIdx, sig_io_uopwriteback_bits_debug_seqNum_seqNum, sig_io_uopwriteback_bits_perfDebugInfo_tlbRespTime, sig_io_uopwriteback_bits_perfDebugInfo_tlbFirstReqTime, sig_io_uopwriteback_bits_perfDebugInfo_runahead_checkpoint_id, sig_io_uopwriteback_bits_perfDebugInfo_issueTime, sig_io_uopwriteback_bits_perfDebugInfo_selectTime, sig_io_uopwriteback_bits_perfDebugInfo_enqRsTime, sig_io_uopwriteback_bits_perfDebugInfo_dispatchTime, sig_io_uopwriteback_bits_perfDebugInfo_renameTime, sig_io_uopwriteback_bits_perfDebugInfo_eliminatedMove, sig_io_uopwriteback_bits_debug_vaddr, sig_io_uopwriteback_bits_debug_paddr, sig_io_uopwriteback_bits_debug_isPerfCnt, sig_io_uopwriteback_bits_debug_isNCIO, sig_io_uopwriteback_bits_debug_isMMIO, sig_io_uopwriteback_bits_vls_isVlm, sig_io_uopwriteback_bits_vls_isVecLoad, sig_io_uopwriteback_bits_vls_isWhole, sig_io_uopwriteback_bits_vls_isStrided, sig_io_uopwriteback_bits_vls_isMasked, sig_io_uopwriteback_bits_vls_isIndexed, sig_io_uopwriteback_bits_vls_vdIdxInField, sig_io_uopwriteback_bits_vls_vdIdx, sig_io_uopwriteback_bits_vls_vpu_sew64, sig_io_uopwriteback_bits_vls_vpu_sew32, sig_io_uopwriteback_bits_vls_vpu_sew16, sig_io_uopwriteback_bits_vls_vpu_sew8, sig_io_uopwriteback_bits_vls_vpu_maskVecGen, sig_io_uopwriteback_bits_vls_vpu_isVleff, sig_io_uopwriteback_bits_vls_vpu_isWritePartVd, sig_io_uopwriteback_bits_vls_vpu_isDependOldVd, sig_io_uopwriteback_bits_vls_vpu_isMove, sig_io_uopwriteback_bits_vls_vpu_isOpMask, sig_io_uopwriteback_bits_vls_vpu_isDstMask, sig_io_uopwriteback_bits_vls_vpu_isNarrow, sig_io_uopwriteback_bits_vls_vpu_isExt, sig_io_uopwriteback_bits_vls_vpu_isReverse, sig_io_uopwriteback_bits_vls_vpu_veew, sig_io_uopwriteback_bits_vls_vpu_nf, sig_io_uopwriteback_bits_vls_vpu_vl, sig_io_uopwriteback_bits_vls_vpu_vmask, sig_io_uopwriteback_bits_vls_vpu_lastUop, sig_io_uopwriteback_bits_vls_vpu_vuopIdx, sig_io_uopwriteback_bits_vls_vpu_vxrm, sig_io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_8, sig_io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_4, sig_io_uopwriteback_bits_vls_vpu_fpu_isFoldTo1_2, sig_io_uopwriteback_bits_vls_vpu_fpu_isReduction, sig_io_uopwriteback_bits_vls_vpu_fpu_isFP64Instr, sig_io_uopwriteback_bits_vls_vpu_fpu_isFP32Instr, sig_io_uopwriteback_bits_vls_vpu_fpu_isFpToVecInst, sig_io_uopwriteback_bits_vls_vpu_frm, sig_io_uopwriteback_bits_vls_vpu_vstart, sig_io_uopwriteback_bits_vls_vpu_vm, sig_io_uopwriteback_bits_vls_vpu_specVlmul, sig_io_uopwriteback_bits_vls_vpu_specVsew, sig_io_uopwriteback_bits_vls_vpu_specVta, sig_io_uopwriteback_bits_vls_vpu_specVma, sig_io_uopwriteback_bits_vls_vpu_specVill, sig_io_uopwriteback_bits_vls_vpu_vlmul, sig_io_uopwriteback_bits_vls_vpu_vsew, sig_io_uopwriteback_bits_vls_vpu_vta, sig_io_uopwriteback_bits_vls_vpu_vma, sig_io_uopwriteback_bits_vls_vpu_vill, sig_io_uopwriteback_bits_trigger, sig_io_uopwriteback_bits_exceptionVec_23, sig_io_uopwriteback_bits_exceptionVec_21, sig_io_uopwriteback_bits_exceptionVec_19, sig_io_uopwriteback_bits_exceptionVec_15, sig_io_uopwriteback_bits_exceptionVec_13, sig_io_uopwriteback_bits_exceptionVec_7, sig_io_uopwriteback_bits_exceptionVec_5, sig_io_uopwriteback_bits_exceptionVec_3, sig_io_uopwriteback_bits_vlWen, sig_io_uopwriteback_bits_v0Wen, sig_io_uopwriteback_bits_vecWen, sig_io_uopwriteback_bits_robIdx_value, sig_io_uopwriteback_bits_robIdx_flag, sig_io_uopwriteback_bits_pdestVl, sig_io_uopwriteback_bits_pdest, sig_io_uopwriteback_bits_data_0, sig_io_uopwriteback_valid};

endmodule
