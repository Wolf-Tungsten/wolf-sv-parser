`timescale 1ns/1ps

module ExprCastBug (
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
    wire [7:0] dist_val = (flag_a == flag_b)
        ? 8'(val_a - val_b)
        : 8'(8'(val_a - 8'h20) - val_b);

    wire [5:0] inner = 6'({1'b0, b0} + {1'b0, 5'({1'b0, b1} + {1'b0, b2})});
    wire [8:0] sum_cast = 9'({1'b0, dist_val} + {3'b0, inner});
    assign sum = sum_cast;
    assign bad = (9'({1'b0, dist_val} + {3'b0, inner}) != 9'd224);

    // Keep unused control signals from being optimized away in some tools.
    wire _unused_ctrl = clk ^ rst_n;
    wire _unused_sum = ^sum_cast ^ _unused_ctrl;
    wire _unused_bad = bad ^ _unused_sum;
endmodule
