// Testbench wrapper for CASE_008 - directly instantiate DUT with public signals
module xs_bugcase_tb (
    input wire clk,
    input wire rst_n,
    output wire [7:0] counter_val,
    output wire is_zero
);
    reg_init_test dut (
        .clk(clk),
        .rst_n(rst_n),
        .counter_val(counter_val),
        .is_zero(is_zero)
    );
endmodule
