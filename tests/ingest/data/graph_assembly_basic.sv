module graph_assembly_basic(
    input  logic clk,
    input  logic a,
    input  logic b,
    input  logic en,
    output logic y,
    output logic q,
    output logic l
);
    assign y = a & b;

    always_ff @(posedge clk) begin
        if (en)
            q <= a;
    end

    always_latch begin
        if (en)
            l <= b;
    end
endmodule
