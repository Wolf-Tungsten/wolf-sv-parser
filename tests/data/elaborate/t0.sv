module t0 #(parameter [3:0] P = 16) (
    input  wire [7:0] i_port,
    output wire [7:0] o_port
);
    assign o_port = ~i_port + P;
endmodule
