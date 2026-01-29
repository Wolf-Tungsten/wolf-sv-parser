module stmt_lowerer_pattern_if(
    input logic a,
    input logic b,
    output logic y
);
    always_comb begin
        if (a matches 1'b1)
            y = b;
        else
            y = ~b;
    end
endmodule

module stmt_lowerer_pattern_case(
    input logic [1:0] sel,
    output logic y
);
    always_comb begin
        case (sel) matches
            2'b00: y = 1'b0;
            default: y = 1'b1;
        endcase
    end
endmodule

module stmt_lowerer_while_stmt(
    input logic a,
    input logic b,
    output logic y
);
    always @* begin
        while (a) begin
            y = b;
        end
    end
endmodule

module stmt_lowerer_do_while_stmt(
    input logic a,
    input logic b,
    output logic y
);
    always @* begin
        do begin
            y = b;
        end while (a);
    end
endmodule

module stmt_lowerer_forever_stmt(
    input logic b,
    output logic y
);
    always @* begin
        forever begin
            y = b;
        end
    end
endmodule
