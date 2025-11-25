# 添加 dlatch 支持

## 参考 yosys 中的 dlatch 原语

```
D-type latches are represented by $dlatch cells. These cells have an enable port EN, an input port D, and an output port Q. The following parameters are available for $dlatch cells:

WIDTH
The width of input D and output Q.

EN_POLARITY
The enable input is active-high if this parameter has the value 1'b1 and active-low if this parameter is 1'b0.

The latch is transparent when the EN input is active.

module \$dlatch (EN, D, Q);

    parameter WIDTH = 0;
    parameter EN_POLARITY = 1'b1;

    input EN;
    input [WIDTH-1:0] D;
    output reg [WIDTH-1:0] Q;

    always @* begin
        if (EN == EN_POLARITY)
            Q = D;
    end

endmodule

D-type latches with reset are represented by $adlatch cells. In addition to $dlatch ports and parameters, they also have a single-bit ARST input port for the reset pin and the following additional parameters:

ARST_POLARITY
The asynchronous reset is active-high if this parameter has the value 1'b1 and active-low if this parameter is 1'b0.

ARST_VALUE
The state of Q will be set to this value when the reset is active.

module \$adlatch (EN, ARST, D, Q);

    parameter WIDTH = 0;
    parameter EN_POLARITY = 1'b1;
    parameter ARST_POLARITY = 1'b1;
    parameter ARST_VALUE = 0;

    input EN, ARST;
    input [WIDTH-1:0] D;
    output reg [WIDTH-1:0] Q;

    always @* begin
        if (ARST == ARST_POLARITY)
            Q = ARST_VALUE;
        else if (EN == EN_POLARITY)
            Q = D;
    end

```
## 在 GRH 表示中添加

- kLatch：位宽可配置，en 的电平通过 enLevel = high/low 配置
- kLatchArst：位宽可配置，en 的电平通过 enLevel = high/low 配置，Arst的极性通过 rstPolarity 配置

