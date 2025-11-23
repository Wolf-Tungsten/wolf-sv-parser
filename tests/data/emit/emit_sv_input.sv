module emit_sv_child(
    input  logic cin,
    output logic cout
);
  assign cout = cin;
endmodule

module emit_sv_top(
    input  logic        clk,
    input  logic        rst,
    input  logic        en,
    input  logic [7:0]  din,
    input  logic [1:0]  addr,
    input  logic [7:0]  mask,
    output logic [7:0]  dout,
    output logic [7:0]  async_dout,
    output logic        cout
);
  logic [7:0] mem [0:3];

  emit_sv_child u_child(
      .cin(en),
      .cout(cout)
  );

  always_ff @(posedge clk) begin
    if (rst) begin
      dout <= 8'd0;
    end else if (en) begin
      dout <= din;
    end
  end

  always_ff @(posedge clk or negedge rst) begin
    if (!rst) begin
      async_dout <= 8'd0;
    end else begin
      async_dout <= din;
    end
  end

  always_ff @(posedge clk) begin
    if (en) begin
      mem[addr] <= din;
    end
  end
endmodule
