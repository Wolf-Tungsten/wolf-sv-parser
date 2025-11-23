typedef struct packed {
    logic [7:0] lane[2];
} vec2_t;

module dpic_stage24(
    input  logic        clk,
    input  logic        en,
    input  vec2_t       lhs_vec,
    input  logic [7:0]  rhs_scalar,
    output vec2_t       sum_vec
);
    import "DPI-C" function void dpic_add(
        input  vec2_t       lhs_vec,
        input  logic [7:0]  rhs_scalar,
        output vec2_t       result_vec
    );

    vec2_t sum_storage;
    assign sum_vec = sum_storage;

    always_ff @(posedge clk) begin
        if (en) begin
            dpic_add(lhs_vec, rhs_scalar, sum_storage);
        end
    end
endmodule
