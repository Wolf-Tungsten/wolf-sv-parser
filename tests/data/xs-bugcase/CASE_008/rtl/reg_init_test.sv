// CASE_008: Register Initialization Test
// This module tests whether registers are properly initialized with initial block

module reg_init_test (
    input  wire        clk,
    input  wire        rst_n,
    output wire [7:0]  counter_val,
    output wire        is_zero
);

    // Register with initialization
    reg [7:0] counter;
    
    // Initialize counter to 0
    initial begin
        counter = 8'h00;
    end
    
    // Simple counter
    always @(posedge clk) begin
        if (~rst_n) begin
            counter <= 8'h00;
        end else begin
            counter <= counter + 8'h01;
        end
    end
    
    // Output assignments
    assign counter_val = counter;
    assign is_zero = (counter == 8'h00);

endmodule
