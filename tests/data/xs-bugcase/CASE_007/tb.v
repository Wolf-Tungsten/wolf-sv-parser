`timescale 1ns/1ps

module xs_bugcase_tb (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [4:0]  read_addr,
    output wire [111:0] read_data,
    output wire        is_zero,
    output wire        has_x
);

    sram_init_test dut (
        .clk       (clk),
        .rst_n     (rst_n),
        .read_addr (read_addr),
        .read_data (read_data),
        .is_zero   (is_zero),
        .has_x     (has_x)
    );

endmodule
