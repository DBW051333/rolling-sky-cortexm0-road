
module AHBlite_UART_LED (
    input  wire        HCLK,
    input  wire        HRESETn,
    input  wire        HSEL,
    input  wire [31:0] HADDR,
    input  wire [1:0]  HTRANS,
    input  wire [2:0]  HSIZE,
    input  wire [3:0]  HPROT,
    input  wire        HWRITE,
    input  wire [31:0] HWDATA,
    input  wire        HREADY,
    output wire        HREADYOUT,
    output reg  [31:0] HRDATA,
    output wire        HRESP,
    output wire [7:0]  uart_tx_data,
    output wire        uart_tx_valid,
    input  wire        uart_tx_ready,
    input  wire [7:0]  uart_rx_data,
    input  wire        uart_rx_valid,
    input  wire        uart_rx_frame_error,
    output wire        uart_irq,
    output wire [7:0]  led_out
);

    localparam [7:0] TXDATA  = 8'h00;
    localparam [7:0] RXDATA  = 8'h04;
    localparam [7:0] STATUS  = 8'h08;
    localparam [7:0] LEDDATA = 8'h0c;

    assign HREADYOUT = 1'b1;
    assign HRESP = 1'b0;

    wire address_phase = HSEL & HTRANS[1] & HREADY &
                         (HSIZE == 3'b010) & (HADDR[1:0] == 2'b00);
    reg [7:0] address_reg;
    reg write_phase;
    reg read_phase;

    always @(posedge HCLK or negedge HRESETn) begin
        if (!HRESETn) begin
            address_reg <= 8'd0;
            write_phase <= 1'b0;
            read_phase  <= 1'b0;
        end else if (HREADY) begin
            write_phase <= address_phase & HWRITE;
            read_phase  <= address_phase & ~HWRITE;
            if (address_phase)
                address_reg <= HADDR[7:0];
        end
    end

    wire write_tx     = write_phase & HREADY & (address_reg == TXDATA);
    wire write_status = write_phase & HREADY & (address_reg == STATUS);
    wire write_led    = write_phase & HREADY & (address_reg == LEDDATA);
    wire read_rx      = read_phase  & HREADY & (address_reg == RXDATA);

    reg [7:0] tx_hold;
    reg       tx_pending;
    reg       tx_overrun;

    reg [7:0] rx_hold;
    reg       rx_pending;
    reg       rx_frame_error_hold;
    reg       rx_overrun;
    reg [7:0] led_reg;

    assign led_out = led_reg;
    assign uart_tx_data = tx_hold;
    assign uart_tx_valid = tx_pending;
    // Keep IRQ asserted until software reads RXDATA. This prevents a byte from
    // being missed while the processor is executing the foreground LED delay.
    assign uart_irq = rx_pending;

    always @(posedge HCLK or negedge HRESETn) begin
        if (!HRESETn) begin
            tx_hold             <= 8'd0;
            tx_pending          <= 1'b0;
            tx_overrun          <= 1'b0;
            rx_hold             <= 8'd0;
            rx_pending          <= 1'b0;
            rx_frame_error_hold <= 1'b0;
            rx_overrun          <= 1'b0;
            led_reg             <= 8'd0;
        end else begin

            if (tx_pending && uart_tx_ready)
                tx_pending <= 1'b0;
            if (write_tx) begin
                if (!tx_pending || uart_tx_ready) begin
                    tx_hold    <= HWDATA[7:0];
                    tx_pending <= 1'b1;
                end else begin
                    tx_overrun <= 1'b1;
                end
            end

            if (write_status && HWDATA[4])
                tx_overrun <= 1'b0;
            if (write_status && HWDATA[3])
                rx_overrun <= 1'b0;

            if (read_rx) begin
                rx_pending          <= 1'b0;
                rx_frame_error_hold <= 1'b0;
            end
            if (uart_rx_valid) begin
                if (!rx_pending || read_rx) begin
                    rx_hold             <= uart_rx_data;
                    rx_pending          <= 1'b1;
                    rx_frame_error_hold <= uart_rx_frame_error;
                end else begin
                    rx_overrun <= 1'b1;
                end
            end

            if (write_led)
                led_reg <= HWDATA[7:0];
        end
    end


    always @(*) begin
        case (address_reg)
            RXDATA:  HRDATA = {24'd0, rx_hold};
            STATUS:  HRDATA = {27'd0, tx_overrun, rx_overrun,
                               rx_frame_error_hold, rx_pending, ~tx_pending};
            LEDDATA: HRDATA = {24'd0, led_reg};
            default: HRDATA = 32'd0;
        endcase
    end

endmodule
