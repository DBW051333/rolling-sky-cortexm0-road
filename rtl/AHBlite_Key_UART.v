module AHBlite_Key_UART (
    input HCLK, HRESETn, HSEL, HWRITE, HREADY,
    input [31:0] HADDR, HWDATA,
    input [1:0] HTRANS,
    input [2:0] HSIZE,
    input [3:0] HPROT,
    output HREADYOUT, HRESP,
    output [31:0] HRDATA,
    input [15:0] key_data,
    input [15:0] key_held,
    input [95:0] adc_levels,
    output key_clear,
    output uart_irq,
    input uart_rx,
    output uart_tx
);
    wire transfer = HSEL && HTRANS[1] && HREADY && HSIZE == 3'b010;
    /* Slide-potentiometer samples live just above the key registers:
     * 0x008 + 4*(channel-1) for channels 1..7. They sit below the UART's
     * HADDR[8] window, so the two never overlap. */
    wire adc_hit = HADDR[8:0] >= 9'h008 && HADDR[8:0] <= 9'h020;
    reg key_write, uart_selected,held_selected,adc_selected;
    reg [3:0] adc_index;
    always @(posedge HCLK or negedge HRESETn) begin
        if (!HRESETn) begin
            key_write <= 0; uart_selected <= 0; held_selected<=0;
            adc_selected <= 0; adc_index <= 0;
        end else if (HREADY) begin
            key_write <= transfer && HWRITE && HADDR[8:0] == 0;
            uart_selected <= HADDR[8];
            held_selected <= HADDR[8:0]==9'h004;
            adc_selected <= adc_hit;
            adc_index <= HADDR[5:2];
        end
    end
    assign key_clear = key_write && HREADY && HWDATA[0];
    wire [31:0] uart_data;
    wire [7:0] tx_data, rx_data;
    wire tx_valid, tx_ready, rx_valid, rx_error;
    /* Address 0x008 maps to channel 1, so the index runs 2..8. */
    reg [11:0] adc_word;
    always @(*) begin
        case (adc_index)
        4'd2: adc_word = adc_levels[11:0];    // channel 1
        4'd3: adc_word = adc_levels[23:12];
        4'd4: adc_word = adc_levels[35:24];
        4'd5: adc_word = adc_levels[47:36];
        4'd6: adc_word = adc_levels[59:48];
        4'd7: adc_word = adc_levels[71:60];
        4'd8: adc_word = adc_levels[83:72];   // channel 7
        default: adc_word = 12'd0;
        endcase
    end
    assign HRDATA = uart_selected ? uart_data :
                    adc_selected  ? {20'b0,adc_word} :
                    held_selected ? {16'b0,key_held} : {16'b0,key_data};
    assign HREADYOUT = 1;
    assign HRESP = 0;
    AHBlite_UART_LED registers(
        .HCLK(HCLK), .HRESETn(HRESETn), .HSEL(HSEL && HADDR[8]),
        .HADDR(HADDR), .HTRANS(HTRANS), .HSIZE(HSIZE), .HPROT(HPROT),
        .HWRITE(HWRITE), .HWDATA(HWDATA), .HREADY(HREADY),
        .HREADYOUT(), .HRESP(), .HRDATA(uart_data),
        .uart_tx_data(tx_data), .uart_tx_valid(tx_valid), .uart_tx_ready(tx_ready),
        .uart_rx_data(rx_data), .uart_rx_valid(rx_valid), .uart_rx_frame_error(rx_error),
        .uart_irq(uart_irq), .led_out()
    );
    uart serial_link(.clk(HCLK), .rst_n(HRESETn), .rx(uart_rx), .tx(uart_tx),
        .tx_data(tx_data), .tx_valid(tx_valid), .tx_ready(tx_ready),
        .rx_data(rx_data), .rx_valid(rx_valid), .rx_frame_error(rx_error));
endmodule
