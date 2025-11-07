module nested_leaf (
    input  logic [3:0] leaf_in,
    output logic [3:0] leaf_out
);
    assign leaf_out = leaf_in;
endmodule

module nested_mid (
    input  logic [3:0] mid_in,
    output logic [3:0] mid_out
);
    nested_leaf u_leaf (
        .leaf_in(mid_in),
        .leaf_out(mid_out)
    );
endmodule

module nested_top (
    input  logic [3:0] top_in,
    output logic [3:0] top_out
);
    nested_mid u_mid (
        .mid_in(top_in),
        .mid_out(top_out)
    );
endmodule
