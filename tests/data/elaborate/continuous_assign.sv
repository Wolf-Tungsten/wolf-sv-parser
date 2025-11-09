module assign_stage11_case(
    input  logic [7:0] in_a,
    input  logic [7:0] in_b
);
    wire logic [7:0] scalar_net;
    assign scalar_net = in_a;

    typedef struct packed {
        logic [3:0] hi;
        logic [3:0] lo;
    } pair_t;

    wire pair_t struct_net;
    assign struct_net.hi = in_a[7:4];
    assign struct_net.lo[3:2] = in_b[5:4];
    assign struct_net.lo[1:0] = in_b[1:0];

    wire logic [1:0][3:0] array_net;
    assign array_net[1] = in_b[7:4];
    assign array_net[0][2:1] = in_a[3:2];

    wire logic [3:0] partial_net;
    assign partial_net[3:1] = in_a[5:3];

    wire logic [3:0] concat_a;
    wire logic [3:0] concat_b;
    assign {concat_a, concat_b[1:0]} = {in_a[7:4], in_b[1:0]};
endmodule
