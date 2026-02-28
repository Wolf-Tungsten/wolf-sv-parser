module top_module(
    input logic clk
);
    logic [3:0] counter;

    always_ff @(posedge clk) begin
        counter <= counter + 4'd1;
    end
endmodule
