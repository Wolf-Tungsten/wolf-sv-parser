`timescale 1ns/1ps

module xs_bugcase_tb (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        r_0_enable,
    input  logic [63:0] r_0_index,
    input  logic        w_0_enable,
    input  logic [63:0] w_0_index,
    input  logic [63:0] w_0_data,
    input  logic [63:0] w_0_mask,
    output logic [63:0] r_0_data,
    output logic        r_0_async
);
    Mem1R1WHelperCase006 dut (
        .clock      (clk),
        .reset      (!rst_n),
        .r_0_enable (r_0_enable),
        .r_0_index  (r_0_index),
        .r_0_data   (r_0_data),
        .r_0_async  (r_0_async),
        .w_0_enable (w_0_enable),
        .w_0_index  (w_0_index),
        .w_0_data   (w_0_data),
        .w_0_mask   (w_0_mask)
    );
endmodule
