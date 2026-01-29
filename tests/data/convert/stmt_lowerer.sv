module stmt_lowerer_case(
    input logic a,
    input logic b,
    input logic c,
    input logic d,
    output logic y,
    output logic z,
    output wire w
);
    always_comb begin
        if (a)
            y = b;
        else
            y = c;
        if (b && c)
            z = d;
    end

    assign w = a & b;
endmodule

module stmt_lowerer_if_chain(
    input logic a,
    input logic b,
    input logic c,
    input logic d,
    output logic y,
    output logic z
);
    always_comb begin
        if (a)
            y = b;
        else if (c)
            z = d;
    end
endmodule

module stmt_lowerer_case_stmt(
    input logic [1:0] sel,
    input logic a,
    input logic b,
    input logic c,
    output logic y
);
    always_comb begin
        case (sel)
            2'b00, 2'b01: y = a;
            2'b10: y = b;
            default: y = c;
        endcase
    end
endmodule

module stmt_lowerer_casez_stmt(
    input logic [1:0] sel,
    input logic a,
    input logic b,
    input logic c,
    output logic y
);
    always_comb begin
        casez (sel)
            2'b0?: y = a;
            2'b1?: y = b;
            default: y = c;
        endcase
    end
endmodule

module stmt_lowerer_casex_stmt(
    input logic [1:0] sel,
    input logic a,
    input logic b,
    input logic c,
    output logic y
);
    always_comb begin
        casex (sel)
            2'b0x: y = a;
            2'b1x: y = b;
            default: y = c;
        endcase
    end
endmodule

module stmt_lowerer_casez_2state_stmt(
    input bit [1:0] sel,
    input logic a,
    input logic b,
    input logic c,
    output logic y
);
    always_comb begin
        casez (sel)
            2'b0?: y = a;
            2'b1?: y = b;
            default: y = c;
        endcase
    end
endmodule

module stmt_lowerer_casex_2state_stmt(
    input bit [1:0] sel,
    input logic a,
    input logic b,
    input logic c,
    output logic y
);
    always_comb begin
        casex (sel)
            2'b0x: y = a;
            2'b1x: y = b;
            default: y = c;
        endcase
    end
endmodule

module stmt_lowerer_repeat_stmt(
    input logic a,
    output logic y
);
    always_comb begin
        repeat (3) begin
            y = a;
        end
    end
endmodule

module stmt_lowerer_for_stmt(
    input logic a,
    output logic y
);
    always_comb begin
        for (int i = 0; i < 2; i = i + 1) begin
            y = a;
        end
    end
endmodule

module stmt_lowerer_foreach_stmt(
    input logic a,
    output logic y
);
    logic [7:0] arr [0:1];
    always_comb begin
        foreach (arr[i]) begin
            y = a;
        end
    end
endmodule

module stmt_lowerer_repeat_large_stmt(
    input logic a,
    output logic y
);
    always_comb begin
        repeat (5000) begin
            y = a;
        end
    end
endmodule

module stmt_lowerer_for_large_stmt(
    input logic a,
    output logic y
);
    always_comb begin
        for (int i = 0; i < 5000; i = i + 1) begin
            y = a;
        end
    end
endmodule

module stmt_lowerer_foreach_large_stmt(
    input logic a,
    output logic y
);
    logic [7:0] arr [0:4999];
    always_comb begin
        foreach (arr[i]) begin
            y = a;
        end
    end
endmodule

module stmt_lowerer_case_inside_stmt(
    input logic [7:0] sel,
    input logic a,
    input logic b,
    input logic c,
    input logic d,
    input logic e,
    output logic y
);
    always_comb begin
        case (sel) inside
            8'h00: y = a;
            [8'h10:8'h1f]: y = b;
            [8'h20 +/- 8'h03]: y = c;
            [8'h40 +%- 8'h10]: y = d;
            default: y = e;
        endcase
    end
endmodule

module stmt_lowerer_lhs_select(
    input logic [7:0] data,
    input logic [2:0] idx,
    output logic [7:0] y
);
    always_comb begin
        y[3] = data[0];
        y[7:4] = data[7:4];
        y[idx] = data[1];
        y[idx +: 2] = data[3:2];
        y[idx -: 2] = data[5:4];
    end
endmodule

module stmt_lowerer_for_break(
    input logic a,
    output logic y
);
    always_comb begin
        for (int i = 0; i < 4; i = i + 1) begin
            if (i == 2)
                break;
            y = a;
        end
    end
endmodule

module stmt_lowerer_for_continue(
    input logic a,
    output logic y
);
    always_comb begin
        for (int i = 0; i < 4; i = i + 1) begin
            if (i == 1)
                continue;
            y = a;
        end
    end
endmodule

module stmt_lowerer_foreach_break(
    input logic a,
    output logic y
);
    logic [7:0] arr [0:3];
    always_comb begin
        foreach (arr[i]) begin
            if (i == 2)
                break;
            y = a;
        end
    end
endmodule

module stmt_lowerer_foreach_continue(
    input logic a,
    output logic y
);
    logic [7:0] arr [0:3];
    always_comb begin
        foreach (arr[i]) begin
            if (i == 1)
                continue;
            y = a;
        end
    end
endmodule

module stmt_lowerer_for_break_dynamic(
    input logic a,
    input logic stop,
    output logic y
);
    always_comb begin
        for (int i = 0; i < 3; i = i + 1) begin
            if (stop)
                break;
            y = a;
        end
    end
endmodule

module stmt_lowerer_for_continue_dynamic(
    input logic a,
    input logic skip,
    output logic y
);
    always_comb begin
        for (int i = 0; i < 3; i = i + 1) begin
            if (skip)
                continue;
            y = a;
        end
    end
endmodule

module stmt_lowerer_for_break_case_dynamic(
    input logic [1:0] sel,
    input logic a,
    output logic y
);
    always_comb begin
        for (int i = 0; i < 3; i = i + 1) begin
            case (sel)
                2'b00: break;
                default: y = a;
            endcase
        end
    end
endmodule
