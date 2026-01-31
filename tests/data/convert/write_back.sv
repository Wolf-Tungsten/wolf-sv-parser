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

module write_back_case_comb(
    input logic [1:0] sel,
    input logic a,
    input logic b,
    input logic c,
    output logic y
);
    always_comb begin
        case (sel)
            2'b00: y = a;
            2'b01: y = b;
            default: y = c;
        endcase
    end
endmodule

module write_back_bad_seq(
    input logic d,
    output logic q
);
    initial begin
        q <= d;
    end
endmodule
