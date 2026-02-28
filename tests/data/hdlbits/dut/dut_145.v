module top_module(
    input  logic [2:0] y,
    output logic Y2
);
    logic [2:0] Y;

    assign Y = y;
    assign Y2 = &Y;
endmodule
