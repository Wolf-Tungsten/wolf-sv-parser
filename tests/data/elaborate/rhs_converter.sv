typedef struct packed {
    logic [3:0] hi;
    logic [3:0] lo;
} rhs_struct_t;

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
    rhs_struct_t struct_bus;
    logic [3:0] struct_hi_slice;
    logic [15:0] range_bus;
    logic [7:0] static_slice_res;
    logic [3:0] dyn_offset;
    logic [7:0] dynamic_slice_res;
    logic [7:0] net_array [0:3];
    logic [1:0] array_index;
    logic [7:0] array_slice_res;
    logic [7:0] reg_mem [0:3];
    logic [1:0] mem_addr;
    logic [7:0] mem_read_res;

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
    assign struct_bus = '{hi: net_a[7:4], lo: net_b[3:0]};
    assign struct_hi_slice = struct_bus.hi;
    assign range_bus = {net_a, net_b};
    assign static_slice_res = range_bus[11:4];
    assign dyn_offset = {ctrl_sel, ctrl_sel, ctrl_sel, 1'b0};
    assign dynamic_slice_res = range_bus[dyn_offset +: 8];
    assign net_array[0] = net_a;
    assign net_array[1] = net_b;
    assign net_array[2] = port_c;
    assign net_array[3] = 8'h00;
    assign array_index = ctrl_sel ? 2'd1 : 2'd2;
    assign array_slice_res = net_array[array_index];
    assign mem_addr = array_index;
    assign mem_read_res = reg_mem[mem_addr];

    always_ff @(posedge clk) begin
        seq_reg <= net_a;
        reg_mem[mem_addr] <= net_a;
    end

    assign reg_use = seq_reg + net_b;
endmodule
