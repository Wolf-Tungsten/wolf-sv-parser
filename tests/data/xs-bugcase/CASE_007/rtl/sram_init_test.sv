// CASE_007: SRAM Memory Initialization Test
// This module tests whether Memory array is properly initialized with initial block

module sram_init_test (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [4:0]  read_addr,
    output wire [111:0] read_data,
    output wire        is_zero,
    output wire        has_x
);

    // SRAM instance - same structure as ABTB's array_32x112
    reg [111:0] Memory [0:31];
    reg [4:0]   raddr_d0;
    reg         ren_d0;
    
    // Initialize memory with zeros (simple literal init, no loop)
    // STEP 0055 MVP: simple direct assignment only
    integer i;
    initial begin
        for (i = 0; i < 32; i = i + 1) begin
            Memory[i] = 112'h0;
        end
    end
    
    // Sequential logic - read and write
    // Use synchronous reset to ensure ren_d0 is initialized
    always @(posedge clk) begin
        if (~rst_n) begin
            raddr_d0 <= 5'h0;
            ren_d0 <= 1'b0;
        end else begin
            raddr_d0 <= read_addr;
            ren_d0 <= 1'b1;
        end
    end
    
    // Read output
    assign read_data = ren_d0 ? Memory[raddr_d0] : 112'bx;
    
    // Check if data is all zeros
    assign is_zero = (read_data == 112'h0);
    
    // Check if data contains X (only works in simulation)
    // In Verilator, X is typically treated as 0, but we can detect via ===
    assign has_x = ^read_data === 1'bx;  // XOR reduction, X if any bit is X

endmodule
