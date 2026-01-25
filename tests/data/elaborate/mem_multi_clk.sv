module mem_multi_clk (
    input logic clk_a,
    input logic clk_b,
    input logic [3:0] addr_a,
    input logic [3:0] addr_b,
    input logic [7:0] din_a,
    input logic [7:0] din_b,
    input logic we_a,
    input logic we_b
);
  logic [7:0] mem [0:15];
  logic [7:0] rd_a;

  always_ff @(posedge clk_a) begin
    if (we_a) begin
      mem[addr_a] <= din_a;
    end
    rd_a <= mem[addr_a];
  end

  always_ff @(posedge clk_b) begin
    if (we_b) begin
      mem[addr_b] <= din_b;
    end
  end
endmodule
