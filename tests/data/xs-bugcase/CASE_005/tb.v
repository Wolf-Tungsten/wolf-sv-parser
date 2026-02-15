`timescale 1ns/1ps

module xs_bugcase_tb (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [4:0]  idx,
    output logic [4:0]  id_shift,
    output logic [4:0]  id_port,
    output logic [31:0] sel,
    output logic        bad
);
    PackedIndexBug dut (
        .clk(clk),
        .rst_n(rst_n),
        .idx(idx),
        .id_shift(id_shift),
        .id_port(id_port),
        .sel(sel),
        .bad(bad)
    );
endmodule
