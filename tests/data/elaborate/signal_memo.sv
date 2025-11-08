module memo_child(
    input  logic clk,
    input  logic rst_n
);
    wire w_assign;
    logic signed [7:0] comb_bus;
    logic star_assign;
    logic seq_logic;
    reg   reg_ff;
    logic latch_target;
    logic conflict_signal;

    assign w_assign = rst_n;

    always_comb begin
        comb_bus = {7'd0, w_assign};
    end

    always @(*) begin
        star_assign = comb_bus[0];
    end

    always @(posedge clk) begin
        seq_logic <= star_assign;
    end

    always_ff @(posedge clk) begin
        reg_ff <= seq_logic;
    end

    always @(negedge rst_n) begin
        latch_target <= reg_ff;
    end

    assign conflict_signal = star_assign;
    always_ff @(posedge clk) begin
        conflict_signal <= rst_n;
    end
endmodule

module memo_top(
    input logic clk,
    input logic rst_n
);
    memo_child u_child(
        .clk  (clk),
        .rst_n(rst_n)
    );
endmodule
