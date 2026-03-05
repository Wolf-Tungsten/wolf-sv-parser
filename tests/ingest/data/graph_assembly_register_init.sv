module graph_assembly_register_init(
    output logic [31:0] out
);
    logic [31:0] random_bits = $random;

    always_comb begin
        out = random_bits;
    end
endmodule
