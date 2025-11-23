(* blackbox *)
module blackbox_leaf #(parameter WIDTH = 6, parameter DEPTH = 2) (
    input  logic clk,
    input  logic [WIDTH-1:0] in0,
    input  logic [WIDTH-1:0] in1,
    output logic [WIDTH-1:0] out0
);
endmodule

module blackbox_top #(parameter WIDTH = 6) (
    input  logic clk,
    input  logic [WIDTH-1:0] a,
    input  logic [WIDTH-1:0] b,
    output logic [WIDTH-1:0] y_direct,
    output logic [WIDTH-1:0] y_gen
);
    blackbox_leaf #(.WIDTH(WIDTH), .DEPTH(8)) u_direct (
        .clk(clk),
        .in0(a),
        .in1(b),
        .out0(y_direct)
    );

    genvar idx;
    for (idx = 0; idx < 1; idx = idx + 1) begin : gen_blk
        blackbox_leaf #(.WIDTH(WIDTH), .DEPTH(4)) u_gen (
            .clk(clk),
            .in0(a),
            .in1(b),
            .out0(y_gen)
        );
    end
endmodule
