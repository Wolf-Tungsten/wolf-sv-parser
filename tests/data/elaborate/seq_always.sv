module seq_stage17 (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        rst_sync,
    input  logic [3:0]  lo_data,
    input  logic [3:0]  hi_data,
    output logic [7:0]  reg_full_out,
    output logic [7:0]  reg_partial_out,
    output logic [7:0]  reg_multi_out,
    output logic [7:0]  reg_async_out,
    output logic [7:0]  reg_sync_out
);
    logic [7:0] reg_full;
    logic [7:0] reg_partial;
    logic [7:0] reg_multi;
    logic [7:0] reg_async_rst;
    logic [7:0] reg_sync_rst;

    always_ff @(posedge clk) begin
        reg_full <= {hi_data, lo_data};
        reg_partial[3:0] <= lo_data;
        reg_multi[7:4] <= hi_data;
        reg_multi[3:0] <= lo_data;
    end

    always_ff @(posedge clk) begin
        if (!rst_sync) begin
            reg_sync_rst <= 8'h00;
        end
        else begin
            reg_sync_rst <= {hi_data, lo_data};
        end
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_async_rst <= '0;
        end
        else begin
            reg_async_rst <= {hi_data, lo_data};
        end
    end

    assign reg_full_out = reg_full;
    assign reg_partial_out = reg_partial;
    assign reg_multi_out = reg_multi;
    assign reg_async_out = reg_async_rst;
    assign reg_sync_out = reg_sync_rst;
endmodule

// Stage22: display/write/strobe lowering

// 1) 基本 display：在时序 always 中输出寄存器值
module seq_stage22_display_basic (
    input  logic       clk,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] r;
    always_ff @(posedge clk) begin
        r <= d;
        $display("r=%0d", r);
    end
    assign q = r;
endmodule

// 2) 受 guard 控制的 write：enable 决定 display 触发
module seq_stage22_guarded_write (
    input  logic       clk,
    input  logic       en,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] r;
    always_ff @(posedge clk) begin
        if (en) begin
            r <= d + 8'h1;
            $write("en=%0d d=%0d", en, d);
        end
    end
    assign q = r;
endmodule

// 3) strobe 变体
module seq_stage22_strobe (
    input  logic       clk,
    input  logic [7:0] d
);
    always_ff @(posedge clk) begin
        $strobe("d=%0d", d);
    end
endmodule

// 4) 组合过程中的 display：应被忽略并告警
module comb_stage22_display_warning (
    input  logic clk,
    input  logic d,
    output logic q
);
    always_comb begin
        $display("comb d=%0d", d);
        q = d;
    end
endmodule

// -----------------------
// Stage23: assert lowering
// -----------------------

// 1) 基本断言：在时钟沿检查 r==d
module seq_stage23_assert_basic (
    input  logic       clk,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] r;
    always_ff @(posedge clk) begin
        r <= d;
        assert (r == d);
    end
    assign q = r;
endmodule

// 2) guard 内的断言：if(en) 时才检查
module seq_stage23_assert_guard (
    input  logic       clk,
    input  logic       en,
    input  logic [7:0] d
);
    always_ff @(posedge clk) begin
        if (en) begin
            assert (d != 8'hff) else $error("bad d");
        end
    end
endmodule

// 3) 组合断言：应 warning 忽略
module comb_stage23_assert_warning (
    input  logic a,
    input  logic b,
    output logic y
);
    always_comb begin
        assert (a == b);
        y = a & b;
    end
endmodule

module seq_stage18 (
    input  logic        clk,
    input  logic [3:0]  wr_addr,
    input  logic [3:0]  rd_addr,
    input  logic [3:0]  mask_addr,
    input  logic [2:0]  bit_index,
    input  logic [7:0]  wr_data,
    input  logic        bit_value,
    output logic [7:0]  rd_data
);
    logic [7:0] mem [0:15];
    logic [7:0] rd_reg;

    always_ff @(posedge clk) begin
        mem[wr_addr] <= wr_data;
        mem[mask_addr][bit_index] <= bit_value;
        rd_reg <= mem[rd_addr];
    end

    assign rd_data = rd_reg;
endmodule

// Stage19: if/case enable + mask scenarios

// 1) if (en) r <= d; 期望寄存器保持语义（未使能保持上一拍）
module seq_stage19_if_en_reg (
    input  logic       clk,
    input  logic       en,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] r;
    always_ff @(posedge clk) begin
        if (en) begin
            r <= d;
        end
    end
    assign q = r;
endmodule

// Stage20: loops (for/foreach) with static break/continue and last-write-wins

// 1) for 循环 + continue：最后一次写入应覆盖之前的写
module seq_stage20_for_last_write (
    input  logic       clk,
    input  logic [7:0] d0,
    input  logic [7:0] d2,
    output logic [7:0] q
);
    logic [7:0] r;
    always_ff @(posedge clk) begin
        for (int i = 0; i < 3; i++) begin
            if (i == 1) begin
                continue; // 静态 continue：跳过 i==1 迭代
            end
            if (i == 0) begin
                r <= d0;
            end
            else begin
                r <= d2; // 最后一次写
            end
        end
    end
    assign q = r;
endmodule

// 2) foreach + 静态 break：仅更新低 4 bit，高 4 bit 应保持（Q）
module seq_stage20_foreach_partial (
    input  logic       clk,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] r;
    always_ff @(posedge clk) begin
        foreach (r[i]) begin
            if (i >= 4) begin
                break; // 静态 break：裁剪后续迭代
            end
            r[i] <= d[i];
        end
    end
    assign q = r;
endmodule

// 3) for 循环内的 memory 多次写与读：应生成两个写端口和一个同步读端口
module seq_stage20_for_memory (
    input  logic        clk,
    input  logic [3:0]  base,
    input  logic [7:0]  w0,
    input  logic [7:0]  w1,
    output logic [7:0]  probe
);
    logic [7:0] mem [0:15];
    logic [7:0] rd;
    always_ff @(posedge clk) begin
        for (int i = 0; i < 2; i++) begin
            if (i == 0) begin
                mem[base + i] <= w0;
            end
            else begin
                mem[base + i] <= w1;
            end
        end
        rd <= mem[base + 1];
    end
    assign probe = rd;
endmodule

// 2) if (en) mem[...] <= wr_data; if (en2) mem[...][i] <= bit_value;
//    期望写端口/掩码端口的 en 分别为 en/en2；读口在 en_rd 下更新
module seq_stage19_if_en_mem (
    input  logic        clk,
    input  logic        en_wr,
    input  logic        en_bit,
    input  logic        en_rd,
    input  logic [3:0]  wr_addr,
    input  logic [3:0]  rd_addr,
    input  logic [3:0]  mask_addr,
    input  logic [2:0]  bit_index,
    input  logic [7:0]  wr_data,
    input  logic        bit_value,
    output logic [7:0]  rd_data
);
    logic [7:0] mem [0:15];
    logic [7:0] rd_reg;

    always_ff @(posedge clk) begin
        if (en_wr) begin
            mem[wr_addr] <= wr_data;
        end
        if (en_bit) begin
            mem[mask_addr][bit_index] <= bit_value;
        end
        if (en_rd) begin
            rd_reg <= mem[rd_addr];
        end
    end

    assign rd_data = rd_reg;
endmodule

// 3) case 分支：不同分支写不同端口，default 不写
module seq_stage19_case_mem (
    input  logic        clk,
    input  logic [1:0]  sel,
    input  logic [3:0]  addr,
    input  logic [2:0]  bit_index,
    input  logic [7:0]  w0,
    input  logic        b1,
    output logic [7:0]  probe
);
    logic [7:0] mem [0:15];
    logic [7:0] rd;
    always_ff @(posedge clk) begin
        case (sel)
            2'd0: mem[addr] <= w0;              // 整字写：en 为 (sel==0)
            2'd1: mem[addr][bit_index] <= b1;   // 按位写：en 为 (sel==1)
            default: /* no write */;
        endcase
        rd <= mem[addr];
    end
    assign probe = rd;
endmodule

// 4) casez/casex 通配：使能来自通配匹配
module seq_stage19_casez_mem (
    input  logic        clk,
    input  logic [3:0]  sel,     // 4 bit，Z/X 作为通配
    input  logic [3:0]  addr,
    input  logic [7:0]  wdata0,
    input  logic [7:0]  wdataA,
    output logic [7:0]  probe
);
    logic [7:0] mem [0:15];
    logic [7:0] rd;
    always_ff @(posedge clk) begin
        // 对应匹配 4'b0???: 任一 0xxx 均写 wdata0；4'b101?: 写 wdataA
        casez (sel)
            4'b0???: mem[addr] <= wdata0;
            4'b101?: mem[addr] <= wdataA;
            default: /* no write */;
        endcase
        rd <= mem[addr];
    end
    assign probe = rd;
endmodule

// 5) 复位 + 使能：if (rst) r<=0; else if (en) r<=d;
module seq_stage19_rst_en_reg (
    input  logic       clk,
    input  logic       rst,   // 同步高有效复位
    input  logic       en,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] r;
    always_ff @(posedge clk) begin
        if (rst) begin
            r <= '0;
        end
        else if (en) begin
            r <= d;
        end
    end
    assign q = r;
endmodule

// Stage21: Enable registers as primitives

// 1) 仅使能寄存器：if (en) r <= d; 期望生成 kRegisterEn
module seq_stage21_en_reg (
    input  logic       clk,
    input  logic       en,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] r;
    always_ff @(posedge clk) begin
        if (en) begin
            r <= d;
        end
    end
    assign q = r;
endmodule

// 2) 异步低有效复位 + 使能：if (!rst_n) r<=rv; else if (en) r<=d; 期望生成 kRegisterEnArst
module seq_stage21_rst_en_reg (
    input  logic       clk,
    input  logic       rst_n, // 低有效异步复位
    input  logic       en,
    input  logic [7:0] d,
    input  logic [7:0] rv,
    output logic [7:0] q
);
    logic [7:0] r;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            r <= rv;
        end
        else if (en) begin
            r <= d;
        end
    end
    assign q = r;
endmodule

// Stage27: memory address/clkPolarity normalization
module seq_stage27_mem_addr (
    input  logic               clk,
    input  logic signed [31:0] addr_in,
    input  logic        [7:0]  wdata,
    input  logic               bit_value,
    output logic        [7:0]  rdata
);
    logic [7:0] mem [0:99]; // 100 rows -> addr width 7
    always_ff @(posedge clk) begin
        mem[addr_in] <= wdata;
        mem[addr_in][2] <= bit_value;
        rdata <= mem[addr_in + 1];
    end
endmodule

// Stage29: memory ports with reset semantics
module seq_stage29_arst_mem (
    input  logic        clk,
    input  logic        rst_n,      // 异步低有效复位
    input  logic [3:0]  addr_rst,
    input  logic [7:0]  data_rst,
    input  logic [3:0]  addr_wr,
    input  logic [3:0]  addr_bit,
    input  logic [3:0]  addr_rd,
    input  logic        en_wr,
    input  logic        en_bit,
    input  logic        bit_value,
    output logic [7:0]  q
);
    logic [7:0] mem [0:15];
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mem[addr_rst] <= data_rst;       // 复位分支写整字
            mem[addr_rst][0] <= 1'b0;         // 复位分支按位写
        end
        else begin
            if (en_wr) begin
                mem[addr_wr] <= data_rst;
            end
            if (en_bit) begin
                mem[addr_bit][2] <= bit_value;
            end
            q <= mem[addr_rd];
        end
    end
endmodule

module seq_stage29_rst_mem (
    input  logic        clk,
    input  logic        rst,        // 同步高有效复位
    input  logic [3:0]  addr,
    input  logic [7:0]  wdata,
    input  logic        bit_value,
    output logic [7:0]  rdata
);
    logic [7:0] mem [0:15];
    always_ff @(posedge clk) begin
        if (rst) begin
            mem[addr] <= '0;             // 复位分支写整字
            mem[addr][1] <= 1'b0;        // 复位分支按位写
            rdata <= '0;
        end
        else begin
            mem[addr] <= wdata;
            mem[addr][1] <= bit_value;
            rdata <= mem[addr];
        end
    end
endmodule
