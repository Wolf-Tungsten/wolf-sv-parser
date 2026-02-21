module graph_assembly_instance_dedup (
    input wire clk,
    input wire [7:0] d,
    output wire [7:0] q,
    output wire [3:0] r
);
    wire [7:0] w1;
    wire [7:0] w2;
    wire [7:0] w3;
    wire [3:0] p4a;
    wire [3:0] p4b;
    wire [7:0] p8;

    my_dff8 u_dff0(.clk(clk), .d(d),      .q(w1));
    my_dff8 u_dff1(.clk(clk), .d(w1),     .q(w2));
    my_dff8 u_dff2(.clk(clk), .d(w2),     .q(w3));

    my_param #(.WIDTH(4)) u_param0(.clk(clk), .d(d[3:0]),  .q(p4a));
    my_param #(.WIDTH(4)) u_param1(.clk(clk), .d(w1[3:0]), .q(p4b));
    my_param #(.WIDTH(8)) u_param2(.clk(clk), .d(w2),      .q(p8));

    assign q = w3;
    assign r = p4a ^ p4b;
endmodule

module my_dff8 (
    input wire clk,
    input wire [7:0] d,
    output wire [7:0] q
);
    assign q = d;
endmodule

module my_param #(
    parameter int WIDTH = 4
) (
    input wire clk,
    input wire [WIDTH-1:0] d,
    output wire [WIDTH-1:0] q
);
    assign q = d;
endmodule
