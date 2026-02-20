module Mem1R1WHelperCase006 (
    input  logic        clock,
    input  logic        reset,
    input  logic        r_0_enable,
    input  logic [63:0] r_0_index,
    output logic [63:0] r_0_data,
    output logic        r_0_async,
    input  logic        w_0_enable,
    input  logic [63:0] w_0_index,
    input  logic [63:0] w_0_data,
    input  logic [63:0] w_0_mask
);
    localparam int RAM_SIZE = 1024;

    Mem1R1WHelper #(
        .RAM_SIZE(RAM_SIZE)
    ) mem_0 (
        .clock      (clock),
        .r_0_enable (r_0_enable),
        .r_0_index  (r_0_index),
        .r_0_data   (r_0_data),
        .r_0_async  (r_0_async),
        .w_0_enable (w_0_enable),
        .w_0_index  (w_0_index),
        .w_0_data   (w_0_data),
        .w_0_mask   (w_0_mask)
    );

    // reset is unused but kept for testbench symmetry
    wire _unused_reset = reset;
endmodule
