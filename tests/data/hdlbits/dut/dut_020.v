module mod_a(
    input  logic in,
    output logic out
);
    assign out = in;
endmodule

module top_module(
    input  logic in,
    output logic out
);
    mod_a inst(
        .in(in),
        .out(out)
    );
endmodule
