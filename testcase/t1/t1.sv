module t1 (
    input wire [7:0] i_port,
    output wire [7:0] o_port
);

    assign {o_port[3:0], o_port[7:4]} = {1'b1, i_port[6:0]};
endmodule