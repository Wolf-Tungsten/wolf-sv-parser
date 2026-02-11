`timescale 1ns/1ps

module xs_bugcase_tb (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        enable,
    input  logic        init_done,
    input  logic        r_0_enable,
    input  logic [63:0] r_0_index,
    input  logic        w_0_enable,
    input  logic [63:0] w_0_index,
    input  logic [63:0] w_0_data,
    input  logic [63:0] w_0_mask,
    input  logic        jtag_TDO_data,
    input  logic        jtag_TDO_driven,
    output logic [63:0] r_0_data,
    output logic        r_0_async,
    output logic        jtag_TCK,
    output logic        jtag_TMS,
    output logic        jtag_TDI,
    output logic        jtag_TRSTn,
    output logic [31:0] exit
);

    XsDpiTop dut (
        .clock         (clk),
        .reset         (!rst_n),
        .enable        (enable),
        .init_done     (init_done),
        .r_0_enable    (r_0_enable),
        .r_0_index     (r_0_index),
        .r_0_data      (r_0_data),
        .r_0_async     (r_0_async),
        .w_0_enable    (w_0_enable),
        .w_0_index     (w_0_index),
        .w_0_data      (w_0_data),
        .w_0_mask      (w_0_mask),
        .jtag_TDO_data (jtag_TDO_data),
        .jtag_TDO_driven(jtag_TDO_driven),
        .jtag_TCK      (jtag_TCK),
        .jtag_TMS      (jtag_TMS),
        .jtag_TDI      (jtag_TDI),
        .jtag_TRSTn    (jtag_TRSTn),
        .exit          (exit)
    );

endmodule
