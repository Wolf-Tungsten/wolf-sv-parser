`timescale 1ns/1ps

module xs_bugcase_tb (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       flag_a,
    input  logic       flag_b,
    input  logic [7:0] val_a,
    input  logic [7:0] val_b,
    input  logic [4:0] b0,
    input  logic [4:0] b1,
    input  logic [4:0] b2,
    output logic [8:0] sum,
    output logic       bad
);
    ExprCastBug dut (
        .clk(clk),
        .rst_n(rst_n),
        .flag_a(flag_a),
        .flag_b(flag_b),
        .val_a(val_a),
        .val_b(val_b),
        .b0(b0),
        .b1(b1),
        .b2(b2),
        .sum(sum),
        .bad(bad)
    );
endmodule
