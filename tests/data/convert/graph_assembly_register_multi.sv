module graph_assembly_register_multi(
    input  logic clk_a,
    input  logic clk_b,
    input  logic en_a,
    input  logic en_b,
    input  logic d_a,
    input  logic d_b,
    output logic q
);
    always_ff @(posedge clk_a) begin
        if (en_a)
            q <= d_a;
    end

    always_ff @(posedge clk_b) begin
        if (en_b)
            q <= d_b;
    end
endmodule
