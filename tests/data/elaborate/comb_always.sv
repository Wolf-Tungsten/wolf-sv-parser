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

module comb_always_stage13_casex(
    input  logic [3:0] sel,
    input  logic [3:0] in_default,
    input  logic [3:0] in_override,
    output logic [3:0] out_casex
);
    always_comb begin
        out_casex = in_default;
        casex (sel)
            4'b1xxx: out_casex = in_override;
        endcase
    end
endmodule

module comb_always_stage13_casez(
    input  logic [3:0] sel,
    input  logic [3:0] in_default,
    input  logic [3:0] in_override,
    output logic [3:0] out_casez
);
    always_comb begin
        out_casez = in_default;
        casez (sel)
            4'b??11: out_casez = in_override;
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

module comb_always_stage14_static_if(
    input  logic [3:0] in_true,
    input  logic [3:0] in_false,
    input  logic [3:0] dyn_a,
    input  logic [3:0] dyn_b,
    input  logic       select,
    output logic [3:0] out_static_true,
    output logic [3:0] out_static_false,
    output logic [3:0] out_mixed
);
    localparam bit ALWAYS_TRUE = 1'b1;
    localparam bit ALWAYS_FALSE = 1'b0;

    always_comb begin
        if (ALWAYS_TRUE) begin
            out_static_true = in_true;
        end
        else begin
            out_static_true = in_false;
        end
    end

    always_comb begin
        if (ALWAYS_FALSE) begin
            out_static_false = in_true;
        end
        else begin
            out_static_false = in_false;
        end
    end

    always_comb begin
        if (ALWAYS_TRUE) begin
            if (select) begin
                out_mixed = dyn_a;
            end
            else begin
                out_mixed = dyn_b;
            end
        end
        else begin
            out_mixed = '0;
        end
    end
endmodule

module comb_always_stage14_static_case(
    input  logic [3:0] in0,
    input  logic [3:0] in1,
    input  logic [3:0] in2,
    input  logic [3:0] in3,
    input  logic [3:0] dyn_a,
    input  logic [3:0] dyn_b,
    input  logic       select,
    output logic [3:0] out_case_const,
    output logic [3:0] out_case_default,
    output logic [3:0] out_case_nested
);
    localparam int MODE_CONST = 2;
    localparam int MODE_DEFAULT = 9;
    localparam int MODE_NESTED = 1;

    always_comb begin
        case (MODE_CONST)
            0: out_case_const = in0;
            1: out_case_const = in1;
            2: out_case_const = in2;
            default: out_case_const = in3;
        endcase
    end

    always_comb begin
        case (MODE_DEFAULT)
            0: out_case_default = in0;
            1: out_case_default = in1;
            default: out_case_default = in3;
        endcase
    end

    always_comb begin
        case (MODE_NESTED)
            0: out_case_nested = in0;
            1: begin
                if (select) begin
                    out_case_nested = dyn_a;
                end
                else begin
                    out_case_nested = dyn_b;
                end
            end
            default: out_case_nested = in3;
        endcase
    end
endmodule
