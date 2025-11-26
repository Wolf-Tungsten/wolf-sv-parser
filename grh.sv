module emit_sv_top (
  input wire [1:0] addr,
  input wire clk,
  input wire [7:0] din,
  input wire en,
  input wire [7:0] mask,
  input wire rst,
  output reg [7:0] async_dout,
  output wire cout,
  output reg [7:0] dout
);

  wire _comb_ctrl_val_and_3_5;
  wire _comb_ctrl_val_not_3_1;
  wire _comb_ctrl_val_not_3_3;
  wire _comb_ctrl_val_not_5_1;
  wire _comb_ctrl_val_not_7_1;
  wire [7:0] async_dout_if_1;
  wire [7:0] convert_4_0;
  wire [7:0] convert_6_1;
  wire [7:0] dout_if_1;
  wire [7:0] dout_if_3;
  wire lnot_6_0;

  reg [7:0] mem [0:3];

  emit_sv_child u_child (
    .cin(en),
    .cout(cout)
  );

  assign _comb_ctrl_val_not_3_1 = !rst;
  assign convert_4_0 = 8'h0;
  assign _comb_ctrl_val_not_3_3 = !en;
  assign _comb_ctrl_val_and_3_5 = _comb_ctrl_val_not_3_1 & en;
  assign dout_if_1 = en ? din : dout;
  assign dout_if_3 = rst ? convert_4_0 : dout_if_1;
  assign lnot_6_0 = !rst;
  assign _comb_ctrl_val_not_5_1 = !lnot_6_0;
  assign convert_6_1 = 8'h0;
  assign async_dout_if_1 = lnot_6_0 ? convert_6_1 : din;
  assign _comb_ctrl_val_not_7_1 = !en;

  always @(posedge clk or negedge rst) begin
    if (!rst) begin
      async_dout <= convert_6_1;
    end else begin
      async_dout <= async_dout_if_1;
    end
  end
  always @(posedge clk) begin
    if (rst) begin
      dout <= convert_4_0;
    end else if (en) begin
      dout <= din;
    end
  end
  always @(posedge clk) begin
    if (en) begin
      mem[addr] <= din;
    end
  end
endmodule

module emit_sv_child (
  input wire cin,
  output wire cout
);

  assign cout = cin;
endmodule
