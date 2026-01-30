module graph_assembly_memory(
    input logic clk,
    input logic en,
    input logic [3:0] addr,
    input logic [7:0] data,
    output logic [7:0] q_comb,
    output logic [7:0] q_seq
);
    logic [7:0] mem [0:15];
    assign q_comb = mem[addr];
    always_ff @(posedge clk) begin
        if (en) begin
            q_seq <= mem[addr];
            mem[addr][3:0] <= data[3:0];
        end
    end
endmodule
