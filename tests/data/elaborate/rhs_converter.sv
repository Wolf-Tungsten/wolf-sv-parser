module rhs_converter_case(
    input logic clk,
    input logic [7:0] port_a,
    input logic [7:0] port_b,
    input logic [7:0] port_c,
    input logic       ctrl_sel
);
    logic [7:0] net_a;
    logic [7:0] net_b;
    logic [7:0] add_res;
    logic [7:0] mix_res;
    logic       flag_res;
    logic [7:0] mux_res;
    logic [15:0] concat_res;
    logic [7:0] replicate_res;
    logic       reduce_res;
    logic [7:0] const_res;
    logic [7:0] seq_reg;
    logic [7:0] reg_use;

    assign net_a = port_a;
    assign net_b = port_b;
    assign add_res = net_a + net_b;
    assign mix_res = (net_a - net_b) ^ (~port_c);
    assign flag_res = (net_a == net_b) && ctrl_sel;
    assign mux_res = ctrl_sel ? net_a : net_b;
    assign concat_res = {net_a, net_b};
    assign replicate_res = {4{ctrl_sel}};
    assign reduce_res = &net_a;
    assign const_res = 8'd170;

    always_ff @(posedge clk) begin
        seq_reg <= net_a;
    end

    assign reg_use = seq_reg + net_b;
endmodule
