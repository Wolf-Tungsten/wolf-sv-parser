module write_back_seq(
    input logic clk,
    input logic rst,
    input logic en,
    input logic d,
    output logic q
);
    always_ff @(posedge clk) begin
        if (rst) begin
            q <= 1'b0;
        end else if (en) begin
            q <= d;
        end
    end
endmodule

module write_back_latch(
    input logic en,
    input logic d,
    output logic q
);
    always_latch begin
        if (en) begin
            q <= d;
        end
    end
endmodule

module write_back_comb(
    output logic one
);
    assign one = 1'b1;
endmodule

module write_back_bad_seq(
    input logic d,
    output logic q
);
    initial begin
        q <= d;
    end
endmodule
