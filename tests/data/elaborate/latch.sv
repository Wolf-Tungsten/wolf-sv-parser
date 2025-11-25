module latch_always_latch(
    input  logic       en,
    input  logic [3:0] d,
    output logic [3:0] q
);
    always_latch begin
        if (en) begin
            q = d;
        end
    end
endmodule

module latch_inferred(
    input  logic       en,
    input  logic [1:0] d,
    output logic [1:0] q
);
    always_comb begin
        if (en) begin
            q = d;
        end
        // else missing on purpose to trigger latch
    end
endmodule

module latch_inferred_arst(
    input  logic       en,
    input  logic       rst,
    input  logic [2:0] d,
    output logic [2:0] q
);
    always_comb begin
        if (rst) begin
            q = 3'h0;
        end
        else if (en) begin
            q = d;
        end
        // else missing on purpose to trigger latch with async reset
    end
endmodule
