`timescale 1ns/1ps

module xs_bugcase_tb (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [31:0] core_id,
    input  wire [39:0] commit_id,
    input  wire        dirty
);

    display_test dut (
        .core_id   (core_id),
        .commit_id (commit_id),
        .dirty     (dirty)
    );

    // Unused signals to keep TB stable
    wire _unused_clk   = clk;
    wire _unused_rst_n = rst_n;

endmodule
