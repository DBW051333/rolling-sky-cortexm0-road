module top(
    input clk_50M,rst_n,SWCLK,uart_rx,
    inout SWDIO,
    input [3:0] col,
    output [3:0] row,
    output uart_tx,
    output unused_panel_bl,
    output audio_pwm,
    input SD_MISO,
    output SD_CLK,SD_CS,SD_MOSI,
    output [3:0] HDMI_P
);
    wire soc_clk,pixel_clk,serial_clk,pll_locked;
    wire video_we,video_ready,video_write_ready,video_idle,video_flush;
    wire [20:0] video_addr;
    wire [8:0] video_data;
    wire [7:0] movie_address;
    wire movie_write;
    wire [31:0] movie_data,movie_result;
    assign unused_panel_bl=1'b0;
    pll_ip clock_generator(.refclk(clk_50M),.reset(~rst_n),.clk0_out(soc_clk),
        .pixel_clk(pixel_clk),.serial_clk(serial_clk),.locked(pll_locked));
    hdmi_game hdmi(.soc_clk(soc_clk),.pixel_clk(pixel_clk),.serial_clk(serial_clk),
        .reset(~rst_n | ~pll_locked),.write_en(video_we),.write_addr(video_addr),
        .write_data(video_data),.movie_address(movie_address),.movie_write(movie_write),.movie_data(movie_data),.movie_result(movie_result),.ready(video_ready),.write_ready(video_write_ready),.flush(video_flush),.idle(video_idle),.hdmi_p(HDMI_P));
    reg [15:0] startup;
    always @(posedge soc_clk or negedge rst_n)
        if(!rst_n) startup<=0;
        else if(!(&startup)) startup<=startup+1'b1;
    CortexM0_SoC #(.CODE_FILE("../firmware/audio_code.hex")) soc(
        .clk(soc_clk),.RSTn(&startup),.SWCLK(SWCLK),.SWDIO(SWDIO),
        .col(col),.row(row),.uart_rx(uart_rx),.uart_tx(uart_tx),
        .SD_CLK(SD_CLK),.SD_CS(SD_CS),.SD_MOSI(SD_MOSI),.SD_MISO(SD_MISO),
        .movie_address(movie_address),.movie_write(movie_write),.movie_data(movie_data),.movie_result(movie_result),
        .video_we(video_we),.video_addr(video_addr),.video_data(video_data),
        .video_ready(video_ready),.video_write_ready(video_write_ready),.video_idle(video_idle),.video_flush(video_flush),.audio_pwm(audio_pwm));
endmodule
