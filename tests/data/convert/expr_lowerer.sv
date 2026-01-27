module expr_lowerer_case(
    input logic a,
    input logic b,
    input logic c,
    output logic [3:0] out_concat,
    output logic out_mux,
    output logic [1:0] out_rep
);
    assign out_concat = {a, b, 1'b0, 1'b1};
    assign out_mux = (a & b) ? ~c : (a | b);
    assign out_rep = {2{a}};
endmodule
