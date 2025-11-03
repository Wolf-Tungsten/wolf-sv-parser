module t5 #(parameter W = 6) (input wire [W-1:0] i_port, output reg [W-1:0] o_port);

always @(*) begin
    for(integer i = 0; i < W; i = i + 1) begin
        o_port[i +: 1] = ~i_port[i +: 1];
    end
end
endmodule