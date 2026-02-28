module top_module(
    input logic clk
);
    logic [1:0] PHT [0:127];
    logic [6:0] GHR;

    always_ff @(posedge clk) begin
        GHR <= GHR + 7'd1;
        PHT[0] <= PHT[0] + 2'd1;
    end
endmodule
