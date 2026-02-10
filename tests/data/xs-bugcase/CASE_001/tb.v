`timescale 1ns/1ps

module xs_bugcase_tb (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [6:0]  RW0_addr,
    input  logic        RW0_en,
    input  logic        RW0_wmode,
    input  logic [75:0] RW0_wmask,
    input  logic [75:0] RW0_wdata,
    output logic [75:0] RW0_rdata
);

    // DUT: sram_array_1p128x76m1s1h0l1b_bpu_ittage_bank1
    sram_array_1p128x76m1s1h0l1b_bpu_ittage_bank1 dut (
        .mbist_dft_ram_bypass   (1'b0),
        .mbist_dft_ram_bp_clken (1'b0),
        .RW0_clk                (clk),
        .RW0_addr               (RW0_addr),
        .RW0_en                 (RW0_en),
        .RW0_wmode              (RW0_wmode),
        .RW0_wmask              (RW0_wmask),
        .RW0_wdata              (RW0_wdata),
        .RW0_rdata              (RW0_rdata)
    );

    // rst_n is currently unused by the DUT; keep it for TB stability.
    wire _unused_rst_n = rst_n;

endmodule
