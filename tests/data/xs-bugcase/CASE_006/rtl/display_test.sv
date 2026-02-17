// Bugcase 006: Test string constant in $display/$fwrite
// Tests that format strings are properly initialized before use in initial blocks

module display_test (
    input  wire [31:0] core_id,
    input  wire [39:0] commit_id,
    input  wire        dirty
);

    // This should print: "Core 0's Commit SHA is: 68b04f5767, dirty: 0"
    // Bug: If string constant is not initialized before initial block executes,
    // it will print: "Core %d's Commit SHA is: %h, dirty: %d"
    initial begin
        $fwrite(32'h80000001, "Core %d's Commit SHA is: %h, dirty: %d\n", core_id, commit_id, dirty);
    end

endmodule
