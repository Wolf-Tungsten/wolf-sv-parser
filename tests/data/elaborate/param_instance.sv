module p_leaf #(
    parameter int WIDTH = 4
) (
    input  logic [WIDTH-1:0] leaf_in,
    output logic [WIDTH-1:0] leaf_out
);
    assign leaf_out = leaf_in;
endmodule

module p_top #(
    parameter int TOP_WIDTH = 4
) (
    input  logic [TOP_WIDTH-1:0] top_in,
    output logic [TOP_WIDTH-1:0] top_out
);
    p_leaf #(
        .WIDTH(TOP_WIDTH)
    ) u_leaf (
        .leaf_in(top_in),
        .leaf_out(top_out)
    );
endmodule
