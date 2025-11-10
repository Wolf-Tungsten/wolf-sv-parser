module comb_always_stage12_case(
    input  logic [3:0] in_a,
    input  logic [3:0] in_b,
    output logic [3:0] out_a,
    output logic [3:0] out_b,
    output logic [3:0] out_or
);
    logic [3:0] temp;
    logic [3:0] capture_a;
    logic [3:0] capture_b;
    logic [3:0] or_value;

    assign out_a = capture_a;
    assign out_b = capture_b;
    assign out_or = or_value;

    always_comb begin
        temp = in_a;
        capture_a = temp;

        temp = in_b;
        capture_b = temp;
    end

    always @(*) begin
        or_value = in_a | in_b;
    end
endmodule
