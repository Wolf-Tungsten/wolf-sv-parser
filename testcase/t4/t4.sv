module sub (input wire sub_i_port, output wire sub_o_port);
    assign sub_o_port = ~sub_i_port;
endmodule

module t4 (
    input wire i_port,
    output wire o_port
);
    wire intm;
    sub sub_0(.sub_i_port(i_port),  .sub_o_port(intm));
    sub sub_1(.sub_i_port(intm), .sub_o_port(o_port));
endmodule