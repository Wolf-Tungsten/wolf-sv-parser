module graph_assembly_slice(
    input logic [7:0] data,
    output logic [7:0] y
);
    always_comb begin
        y = 8'h00;
        y[3] = data[0];
        y[7:4] = data[7:4];
    end
endmodule
