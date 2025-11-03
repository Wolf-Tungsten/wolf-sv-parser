module t3 (
    input logic [7:0] i_port,
    output logic [3:0] o_port
);

    always @(*) begin
        o_port = 1;
        if(i_port > 10) begin
            o_port = 2;
        end else if (i_port > 100) begin
            o_port = 3;
        end
    end
endmodule