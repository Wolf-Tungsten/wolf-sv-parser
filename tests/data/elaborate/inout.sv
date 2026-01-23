module inout_case(
    input  logic        en,
    input  logic [3:0]  data,
    inout  wire  [3:0]  io,
    output logic [3:0]  io_in
);
    assign io = en ? data : 4'bz;
    assign io_in = io;
endmodule
