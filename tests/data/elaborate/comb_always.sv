module comb_always_stage12_case(
    input  logic [3:0] in_a,
    input  logic [3:0] in_b,
    output logic [3:0] out_a,
    output logic [3:0] out_b,
    output logic [3:0] out_or
);
    logic [3:0] temp;
    logic [3:0] capture_a;
    logic [3:0] capture_b;
    logic [3:0] or_value;

    assign out_a = capture_a;
    assign out_b = capture_b;
    assign out_or = or_value;

    always_comb begin
        temp = in_a;
        capture_a = temp;

        temp = in_b;
        capture_b = temp;
    end

    always @(*) begin
        or_value = in_a | in_b;
    end
endmodule

module comb_always_stage13_if(
    input  logic        select,
    input  logic [3:0]  in_a,
    input  logic [3:0]  in_b,
    output logic [3:0]  out_if,
    output logic [3:0]  out_nested
);
    always_comb begin
        if (select) begin
            out_if = in_a;
        end
        else begin
            out_if = in_b;
        end
    end

    always_comb begin
        if (select) begin
            if (in_a[0]) begin
                out_nested = in_a;
            end
            else begin
                out_nested = in_b;
            end
        end
        else begin
            if (in_b[0]) begin
                out_nested = in_b;
            end
            else begin
                out_nested = in_a;
            end
        end
    end
endmodule

module comb_always_stage13_case(
    input  logic [1:0] sel,
    input  logic [3:0] in0,
    input  logic [3:0] in1,
    input  logic [3:0] in2,
    input  logic [3:0] in3,
    output logic [3:0] out_case
);
    always_comb begin
        case (sel)
            2'b00: out_case = in0;
            2'b01: out_case = in1;
            2'b10: out_case = in2;
            default: out_case = in3;
        endcase
    end
endmodule

module comb_always_stage13_default_if(
    input  logic        cond,
    input  logic [3:0]  in_default,
    input  logic [3:0]  in_override,
    output logic [3:0]  out_default
);
    always_comb begin
        out_default = in_default;
        if (cond) begin
            out_default = in_override;
        end
    end
endmodule

module comb_always_stage13_case_defaultless(
    input  logic [1:0] sel,
    input  logic [3:0] in_default,
    input  logic [3:0] in_override,
    output logic [3:0] out_case_implicit
);
    always_comb begin
        out_case_implicit = in_default;
        case (sel)
            2'b01: out_case_implicit = in_override;
        endcase
    end
endmodule

module comb_always_stage13_unique_overlap(
    input  logic [1:0] sel,
    input  logic [3:0] in0,
    input  logic [3:0] in1,
    input  logic [3:0] in2,
    input  logic [3:0] in3,
    output logic [3:0] out_dup
);
    always_comb begin
        unique case (sel)
            2'b00: out_dup = in0;
            2'b00: out_dup = in1;
            2'b01: out_dup = in2;
            default: out_dup = in3;
        endcase
    end
endmodule

module comb_always_stage13_incomplete(
    input  logic        flag,
    input  logic [3:0]  in_bad,
    output logic [3:0]  out_bad
);
    always_comb begin
        if (flag) begin
            out_bad = in_bad;
        end
        // else branch intentionally omitted to trigger latch diagnostic
    end
endmodule
