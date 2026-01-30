module child(
    input logic a,
    output logic y
);
    assign y = a;
endmodule

module child_inout(
    input logic a,
    inout wire io,
    output logic y
);
    assign y = a;
endmodule

module bb #(
    parameter int WIDTH = 4
) (
    input logic [WIDTH-1:0] din,
    output logic [WIDTH-1:0] dout
);
endmodule

module graph_assembly_instance(
    input logic a,
    inout wire io,
    output wire y_child,
    output wire y_inout,
    input logic [3:0] din,
    output wire [3:0] dout
);
    child u_child(
        .a(a),
        .y(y_child)
    );

    child_inout u_child_inout(
        .a(a),
        .io(io),
        .y(y_inout)
    );

    bb #(.WIDTH(4)) u_bb(
        .din(din),
        .dout(dout)
    );
endmodule
