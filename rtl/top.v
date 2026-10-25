module top(
    input clk_50M,rst_n,SWCLK,uart_rx,
    inout SWDIO,
    input [3:0] col,
    output [3:0] row,
    output uart_tx,LCD_CS,LCD_RS,LCD_WR,LCD_RD,LCD_RST,LCD_BL_CTR,
    input SD_MISO,
    output SD_CLK,SD_CS,SD_MOSI,
    output [15:0] LCD_DATA
);
    wire soc_clk;
    pll_ip clock_generator(.refclk(clk_50M),.reset(~rst_n),.clk0_out(soc_clk));
    // Hold the CPU in reset while the PLL and board clock settle.
    reg [15:0] startup;
    always @(posedge soc_clk or negedge rst_n)
        if(!rst_n) startup<=0;
        else if(!(&startup)) startup<=startup+1'b1;
    CortexM0_SoC soc(.clk(soc_clk),.RSTn(&startup),.SWCLK(SWCLK),.SWDIO(SWDIO),
        .col(col),.row(row),.uart_rx(uart_rx),.uart_tx(uart_tx),
        .SD_CLK(SD_CLK),.SD_CS(SD_CS),.SD_MOSI(SD_MOSI),.SD_MISO(SD_MISO),
        .LCD_CS(LCD_CS),.LCD_RS(LCD_RS),.LCD_WR(LCD_WR),.LCD_RD(LCD_RD),
        .LCD_RST(LCD_RST),.LCD_BL_CTR(LCD_BL_CTR),.LCD_DATA(LCD_DATA));
endmodule
