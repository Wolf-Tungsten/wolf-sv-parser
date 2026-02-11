module XsDpiTop (
    input  logic        clock,
    input  logic        reset,
    input  logic        enable,
    input  logic        init_done,
    input  logic        r_0_enable,
    input  logic [63:0] r_0_index,
    output logic [63:0] r_0_data,
    output logic        r_0_async,
    input  logic        w_0_enable,
    input  logic [63:0] w_0_index,
    input  logic [63:0] w_0_data,
    input  logic [63:0] w_0_mask,
    input  logic        jtag_TDO_data,
    input  logic        jtag_TDO_driven,
    output logic        jtag_TCK,
    output logic        jtag_TMS,
    output logic        jtag_TDI,
    output logic        jtag_TRSTn,
    output logic [31:0] exit
);

    localparam int RAM_SIZE = 64;

    Mem1R1WHelper #(
        .RAM_SIZE(RAM_SIZE)
    ) mem (
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

    SimJTAG #(
        .TICK_DELAY(1)
    ) jtag (
        .clock          (clock),
        .reset          (reset),
        .enable         (enable),
        .init_done      (init_done),
        .jtag_TCK       (jtag_TCK),
        .jtag_TMS       (jtag_TMS),
        .jtag_TDI       (jtag_TDI),
        .jtag_TRSTn     (jtag_TRSTn),
        .jtag_TDO_data  (jtag_TDO_data),
        .jtag_TDO_driven(jtag_TDO_driven),
        .exit           (exit)
    );

endmodule
