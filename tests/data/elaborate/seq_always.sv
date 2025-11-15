module seq_stage17 (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        rst_sync,
    input  logic [3:0]  lo_data,
    input  logic [3:0]  hi_data,
    output logic [7:0]  reg_full_out,
    output logic [7:0]  reg_partial_out,
    output logic [7:0]  reg_multi_out,
    output logic [7:0]  reg_async_out,
    output logic [7:0]  reg_sync_out
);
    logic [7:0] reg_full;
    logic [7:0] reg_partial;
    logic [7:0] reg_multi;
    logic [7:0] reg_async_rst;
    logic [7:0] reg_sync_rst;

    always_ff @(posedge clk) begin
        reg_full <= {hi_data, lo_data};
        reg_partial[3:0] <= lo_data;
        reg_multi[7:4] <= hi_data;
        reg_multi[3:0] <= lo_data;
    end

    always_ff @(posedge clk) begin
        if (!rst_sync) begin
            reg_sync_rst <= 8'h00;
        end
        else begin
            reg_sync_rst <= {hi_data, lo_data};
        end
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_async_rst <= '0;
        end
        else begin
            reg_async_rst <= {hi_data, lo_data};
        end
    end

    assign reg_full_out = reg_full;
    assign reg_partial_out = reg_partial;
    assign reg_multi_out = reg_multi;
    assign reg_async_out = reg_async_rst;
    assign reg_sync_out = reg_sync_rst;
endmodule

module seq_stage18 (
    input  logic        clk,
    input  logic [3:0]  wr_addr,
    input  logic [3:0]  rd_addr,
    input  logic [3:0]  mask_addr,
    input  logic [2:0]  bit_index,
    input  logic [7:0]  wr_data,
    input  logic        bit_value,
    output logic [7:0]  rd_data
);
    logic [7:0] mem [0:15];
    logic [7:0] rd_reg;

    always_ff @(posedge clk) begin
        mem[wr_addr] <= wr_data;
        mem[mask_addr][bit_index] <= bit_value;
        rd_reg <= mem[rd_addr];
    end

    assign rd_data = rd_reg;
endmodule
