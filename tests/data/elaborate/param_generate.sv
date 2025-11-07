module pg_leaf #(
    parameter int WIDTH = 1
) (
    input  logic [WIDTH-1:0] in,
    output logic [WIDTH-1:0] out
);
    assign out = in;
endmodule

module pg_top (
    input  logic [3:0] in_small,
    input  logic [7:0] in_big,
    output logic [3:0] out_small,
    output logic [7:0] out_big,
    output logic [3:0] gen0_out,
    output logic [3:0] gen1_out,
    output logic [3:0] arr0_out,
    output logic [3:0] arr1_out
);
    logic [3:0] gen_bus [0:1];
    logic [3:0] arr_bus [0:1];

    pg_leaf #(.WIDTH(4)) inst_small (
        .in(in_small),
        .out(out_small)
    );

    pg_leaf #(.WIDTH(8)) inst_big (
        .in(in_big),
        .out(out_big)
    );

    generate
        genvar idx;
        for (idx = 0; idx < 2; idx++) begin : g_leafs
            pg_leaf #(.WIDTH(4)) inst_loop (
                .in(in_small),
                .out(gen_bus[idx])
            );
        end
    endgenerate

    pg_leaf #(.WIDTH(4)) inst_array [0:1] (
        .in(in_small),
        .out(arr_bus)
    );

    assign gen0_out = gen_bus[0];
    assign gen1_out = gen_bus[1];
    assign arr0_out = arr_bus[0];
    assign arr1_out = arr_bus[1];
endmodule
