module write_back_slice_static(
    input logic [7:0] data,
    output logic [7:0] y
);
    always_comb begin
        y = 8'h00;
        y[3] = data[0];
        y[7:4] = data[7:4];
    end
endmodule

module write_back_slice_dynamic(
    input logic [7:0] data,
    input logic [2:0] idx,
    output logic [7:0] y
);
    always_comb begin
        y = 8'h00;
        y[idx] = data[0];
        y[idx +: 2] = data[2:1];
    end
endmodule

typedef struct packed {
    logic [3:0] hi;
    logic [3:0] lo;
} wb_pair_t;

module write_back_slice_member(
    input logic [3:0] a,
    input logic [3:0] b,
    output wb_pair_t y
);
    always_comb begin
        y.hi = a;
        y.lo = b;
    end
endmodule
