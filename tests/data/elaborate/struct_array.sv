typedef struct packed {
    logic [3:0] part_a;
    logic [1:0] part_b;
} packed_struct_t;

module struct_leaf (
    input  packed_struct_t s_in,
    output packed_struct_t s_out,
    input  logic [1:0][3:0] arr_in,
    output logic [1:0][3:0] arr_out
);
    assign s_out = s_in;
    assign arr_out = arr_in;
endmodule

module struct_top (
    input  packed_struct_t top_struct_in,
    output packed_struct_t top_struct_out,
    input  logic [1:0][3:0] top_arr_in,
    output logic [1:0][3:0] top_arr_out
);
    struct_leaf u_leaf (
        .s_in(top_struct_in),
        .s_out(top_struct_out),
        .arr_in(top_arr_in),
        .arr_out(top_arr_out)
    );
endmodule
