import "DPI-C" function void dpi_capture(input logic [7:0] in_val, output logic [7:0] out_val);
import "DPI-C" function int dpi_add(input int lhs, input int rhs);

module graph_assembly_dpi_display(
    input logic clk,
    input logic [7:0] a,
    input logic [7:0] b,
    output logic [7:0] y
);
    always @(posedge clk) begin
        $display("a=%0d", a);
        $error("oops");
        dpi_capture(a, y);
        dpi_add(a, b);
    end
endmodule
