typedef struct packed {
    logic [3:0] parts_hi;
    logic signed [1:0] parts_lo;
} memo_struct_t;

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
    memo_struct_t net_struct_bus;
    memo_struct_t reg_struct_bus;
    logic [1:0][3:0] net_packed_matrix;
    logic [1:0][3:0] reg_packed_matrix;
    logic signed [2:0] net_unpacked_bus [0:1];
    logic [2:0] reg_unpacked_bus [0:1];

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

    assign net_struct_bus = '{parts_hi: comb_bus[3:0], parts_lo: comb_bus[5:4]};
    always_comb begin
        net_packed_matrix[0] = comb_bus[3:0];
        net_packed_matrix[1] = comb_bus[7:4];
    end
    assign net_unpacked_bus[0] = comb_bus[2:0];
    assign net_unpacked_bus[1] = {star_assign, comb_bus[5:4]};

    always_ff @(posedge clk) begin
        reg_struct_bus <= net_struct_bus;
        reg_packed_matrix <= net_packed_matrix;
        reg_unpacked_bus[0] <= net_unpacked_bus[0];
        reg_unpacked_bus[1] <= net_unpacked_bus[1];
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
