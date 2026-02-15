// Minimal repro for packed array indexing lowering
module PackedIndexPassthru(
    input  logic [4:0] in,
    output logic [4:0] out
);
    assign out = in;
endmodule

module PackedIndexBug(
    input  logic       clk,
    input  logic       rst_n,
    input  logic [4:0] idx,
    output logic [4:0] id_shift,
    output logic [4:0] id_port,
    output logic [31:0] sel,
    output logic       bad
);
    // Same mapping pattern as TLToAXI4_1 _GEN
    wire [31:0][4:0] map = '{
        5'h0, 5'h0, 5'h0, 5'h0, 5'h0, 5'h0, 5'h0, 5'h0,
        5'h0, 5'h0, 5'h0, 5'h0, 5'h0, 5'h0, 5'h0,
        5'h10, 5'hF, 5'hE, 5'hD, 5'hC, 5'hB, 5'hA, 5'h9,
        5'h8, 5'h7, 5'h6, 5'h5, 5'h4, 5'h3, 5'h2, 5'h1, 5'h0
    };

    wire [4:0] map_idx = map[idx];

    PackedIndexPassthru u_passthru (
        .in (map[idx]),
        .out(id_port)
    );

    assign id_shift = map_idx;
    assign sel = 32'h1 << map_idx;
    assign bad = (id_port != map_idx);

    // Unused clock/reset are kept to match the bugcase harness signature.
    wire _unused = clk ^ rst_n;
endmodule
