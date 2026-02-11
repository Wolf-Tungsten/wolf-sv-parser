
`ifdef SYNTHESIS
  `define DISABLE_DIFFTEST_RAM_DPIC
`endif
module Mem1R1WHelper #(
  parameter RAM_SIZE
)(
  input clock,
  
input             r_0_enable,
input      [63:0] r_0_index,
output reg [63:0] r_0_data,
output            r_0_async
,
  
input         w_0_enable,
input  [63:0] w_0_index,
input  [63:0] w_0_data,
input  [63:0] w_0_mask

);
  
`ifdef DISABLE_DIFFTEST_RAM_DPIC
`ifdef PALLADIUM
  initial $ixc_ctrl("tb_import", "$display");
`endif // PALLADIUM

reg [63:0] memory [0 : RAM_SIZE / 8 - 1];

`define MEM_TARGET memory

  string bin_file;
  integer memory_image = 0, n_read = 0, byte_read = 1;
  byte data;
  initial begin
    if ($test$plusargs("workload")) begin
      $value$plusargs("workload=%s", bin_file);
      memory_image = $fopen(bin_file, "rb");
    if (memory_image == 0) begin
      $display("Error: failed to open %s", bin_file);
      $finish;
    end
    foreach (`MEM_TARGET[i]) begin
      if (byte_read == 0) break;
      for (integer j = 0; j < 8; j++) begin
        byte_read = $fread(data, memory_image);
        if (byte_read == 0) break;
        n_read += 1;
        `MEM_TARGET[i][j * 8 +: 8] = data;
      end
    end
    $fclose(memory_image);
    $display("%m: load %d bytes from %s.", n_read, bin_file);
  end
end

`endif // DISABLE_DIFFTEST_RAM_DPIC

  
`ifndef DISABLE_DIFFTEST_RAM_DPIC
import "DPI-C" function longint difftest_ram_read(input longint rIdx);
`endif // DISABLE_DIFFTEST_RAM_DPIC

  
`ifndef DISABLE_DIFFTEST_RAM_DPIC
import "DPI-C" function void difftest_ram_write
(
  input  longint index,
  input  longint data,
  input  longint mask
);
`endif // DISABLE_DIFFTEST_RAM_DPIC

  
`ifdef GSIM
  assign r_0_async = 1'b1;
always @(*) begin
  r_0_data = 0;
`ifndef DISABLE_DIFFTEST_RAM_DPIC
  if (r_0_enable) begin
    r_0_data = difftest_ram_read(r_0_index);
  end
`else
  if (r_0_enable) begin
    r_0_data = `MEM_TARGET[r_0_index];
  end
`endif // DISABLE_DIFFTEST_RAM_DPIC
end
`else // GSIM
  assign r_0_async = 1'b0;
always @(posedge clock) begin
`ifndef DISABLE_DIFFTEST_RAM_DPIC
  if (r_0_enable) begin
    r_0_data <= difftest_ram_read(r_0_index);
  end
`else
  if (r_0_enable) begin
    r_0_data <= `MEM_TARGET[r_0_index];
  end
`endif // DISABLE_DIFFTEST_RAM_DPIC
end
`endif // GSIM

  
always @(posedge clock) begin

`ifndef DISABLE_DIFFTEST_RAM_DPIC
if (w_0_enable) begin
  difftest_ram_write(w_0_index, w_0_data, w_0_mask);
end
`else
if (w_0_enable) begin
  `MEM_TARGET[w_0_index] <= (w_0_data & w_0_mask) | (`MEM_TARGET[w_0_index] & ~w_0_mask);
end
`endif // DISABLE_DIFFTEST_RAM_DPIC

end

endmodule
     