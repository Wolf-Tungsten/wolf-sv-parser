typedef struct packed {
    logic [7:0] data0;
    logic [7:0] data1;
} some_struct_t;

module t2(
    input some_struct_t i_port,
    output logic [7:0] o_port,
    output logic [7:0] another_o_port
);

    assign o_port = i_port[11:4];
    assign another_o_port = i_port.data1;
endmodule