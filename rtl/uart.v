module uart #(
    parameter integer CLK_FREQ_HZ = 32_000_000,
    parameter integer BAUD_RATE   = 115_200
) (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       rx,
    output reg        tx,
    input  wire [7:0] tx_data,
    input  wire       tx_valid,
    output wire       tx_ready,
    output reg  [7:0] rx_data,
    output reg        rx_valid,
    output reg        rx_frame_error
);

    localparam integer BAUD_DIV = (CLK_FREQ_HZ + BAUD_RATE / 2) / BAUD_RATE;
    localparam integer HALF_DIV = BAUD_DIV / 2;

    function integer clog2;
        input integer value;
        integer temp;
        begin
            temp = value - 1;
            for (clog2 = 0; temp > 0; clog2 = clog2 + 1)
                temp = temp >> 1;
        end
    endfunction

    localparam integer COUNT_WIDTH = (BAUD_DIV <= 2) ? 1 : clog2(BAUD_DIV);

    reg [COUNT_WIDTH-1:0] tx_count;
    reg [3:0]             tx_bit_index;
    reg [7:0]             tx_shift;
    reg                   tx_busy;

    assign tx_ready = ~tx_busy;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx           <= 1'b1;
            tx_count     <= {COUNT_WIDTH{1'b0}};
            tx_bit_index <= 4'd0;
            tx_shift     <= 8'd0;
            tx_busy      <= 1'b0;
        end else if (!tx_busy) begin
            tx <= 1'b1;
            if (tx_valid) begin
                tx           <= 1'b0; // Start bit.
                tx_shift     <= tx_data;
                tx_count     <= BAUD_DIV - 1;
                tx_bit_index <= 4'd0;
                tx_busy      <= 1'b1;
            end
        end else if (tx_count != 0) begin
            tx_count <= tx_count - 1'b1;
        end else begin
            tx_count <= BAUD_DIV - 1;
            if (tx_bit_index < 8) begin
                tx           <= tx_shift[0]; // Data bits, least significant first.
                tx_shift     <= {1'b0, tx_shift[7:1]};
                tx_bit_index <= tx_bit_index + 1'b1;
            end else if (tx_bit_index == 8) begin
                tx           <= 1'b1; // Stop bit.
                tx_bit_index <= 4'd9;
            end else begin
                tx      <= 1'b1;
                tx_busy <= 1'b0;
            end
        end
    end

    reg rx_meta;
    reg rx_sync;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rx_meta <= 1'b1;
            rx_sync <= 1'b1;
        end else begin
            rx_meta <= rx;
            rx_sync <= rx_meta;
        end
    end

    localparam [1:0] RX_IDLE  = 2'd0;
    localparam [1:0] RX_START = 2'd1;
    localparam [1:0] RX_DATA  = 2'd2;
    localparam [1:0] RX_STOP  = 2'd3;

    reg [1:0]             rx_state;
    reg [COUNT_WIDTH-1:0] rx_count;
    reg [2:0]             rx_bit_index;
    reg [7:0]             rx_shift;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rx_state       <= RX_IDLE;
            rx_count       <= {COUNT_WIDTH{1'b0}};
            rx_bit_index   <= 3'd0;
            rx_shift       <= 8'd0;
            rx_data        <= 8'd0;
            rx_valid       <= 1'b0;
            rx_frame_error <= 1'b0;
        end else begin
            rx_valid       <= 1'b0;
            rx_frame_error <= 1'b0;
            case (rx_state)
                RX_IDLE: begin
                    if (!rx_sync) begin
                        rx_count <= HALF_DIV - 1;
                        rx_state <= RX_START;
                    end
                end

                RX_START: begin
                    if (rx_count != 0) begin
                        rx_count <= rx_count - 1'b1;
                    end else if (!rx_sync) begin
                        rx_count     <= BAUD_DIV - 1;
                        rx_bit_index <= 3'd0;
                        rx_state     <= RX_DATA;
                    end else begin
                        rx_state <= RX_IDLE; 
                    end
                end

                RX_DATA: begin
                    if (rx_count != 0) begin
                        rx_count <= rx_count - 1'b1;
                    end else begin
                        rx_shift[rx_bit_index] <= rx_sync;
                        rx_count <= BAUD_DIV - 1;
                        if (rx_bit_index == 7)
                            rx_state <= RX_STOP;
                        else
                            rx_bit_index <= rx_bit_index + 1'b1;
                    end
                end

                RX_STOP: begin
                    if (rx_count != 0) begin
                        rx_count <= rx_count - 1'b1;
                    end else begin
                        rx_data        <= rx_shift;
                        rx_valid       <= 1'b1;
                        rx_frame_error <= ~rx_sync;
                        rx_state       <= RX_IDLE;
                    end
                end

                default: rx_state <= RX_IDLE;
            endcase
        end
    end

endmodule
