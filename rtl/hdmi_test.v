// DVI-compatible video over the baseboard HDMI connector (no audio/data islands).
// CEA 1920x1080p30 totals, 74.285714 MHz pixels (~30.01443 Hz).
module hdmi_test (
    input pixel_clk, serial_clk, reset,
    output [3:0] hdmi_p
);
    reg [2:0] pixel_reset;
    always @(posedge pixel_clk or posedge reset)
        if (reset) pixel_reset <= 3'b111;
        else pixel_reset <= {pixel_reset[1:0], 1'b0};

    wire [11:0] x;
    wire [10:0] y;
    wire active, hs, vs;
    wire [7:0] red, green, blue;
    hdmi_test_pattern pattern (
        .clk(pixel_clk), .reset(pixel_reset[2]),
        .x(x), .y(y), .active(active), .hs(hs), .vs(vs),
        .red(red), .green(green), .blue(blue)
    );

    hdmi_tmds_tx tx (.pixel_clk(pixel_clk), .serial_clk(serial_clk),
        .reset(reset), .red(red), .green(green), .blue(blue),
        .active(active), .hs(hs), .vs(vs), .hdmi_p(hdmi_p));
endmodule

// Shared transport for both the bring-up pattern and live game display.
module hdmi_tmds_tx (
    input pixel_clk, serial_clk, reset,
    input [7:0] red, green, blue,
    input active, hs, vs,
    output [3:0] hdmi_p
);
    // Reset each clock domain directly from the asynchronous board/PLL reset.
    // Do not route a pixel-domain reset register into serial-domain async pins.
    reg [2:0] pixel_reset;
    always @(posedge pixel_clk or posedge reset)
        if(reset) pixel_reset<=3'b111;
        else pixel_reset<={pixel_reset[1:0],1'b0};
    wire [9:0] tmds_b, tmds_g, tmds_r;
    hdmi_tmds_encoder enc_b (.clk(pixel_clk), .reset(pixel_reset[2]),
        .data(blue), .de(active), .control({vs,hs}), .symbol(tmds_b));
    hdmi_tmds_encoder enc_g (.clk(pixel_clk), .reset(pixel_reset[2]),
        .data(green), .de(active), .control(2'b00), .symbol(tmds_g));
    hdmi_tmds_encoder enc_r (.clk(pixel_clk), .reset(pixel_reset[2]),
        .data(red), .de(active), .control(2'b00), .symbol(tmds_r));

    // All four lanes share word boundaries, including the forwarded clock.
    // TD inserts each native negative pad from the positive LVDS33 constraint.
    hdmi_serializer serializer (.pixel_clk(pixel_clk), .serial_clk(serial_clk),
        .reset(reset), .words({10'b1111100000,tmds_r,tmds_g,tmds_b}),
        .serial_out(hdmi_p));
endmodule

module hdmi_test_pattern (
    input clk, reset,
    output [11:0] x,
    output [10:0] y,
    output active, hs, vs,
    output reg [7:0] red, green, blue
);
    hdmi_raster raster(clk,reset,x,y,active,hs,vs);
    reg [10:0] marker;
    always @(posedge clk or posedge reset)
        if (reset) marker<=0;
        else if (x==2199 && y==1124) marker<=(marker==1919) ? 0 : marker+1'b1;

    always @* begin
        red = 0; green = 0; blue = 0;
        if (active) begin
            if (y < 720) begin
                // Eight 240-pixel bars: white, yellow, cyan, green,
                // magenta, red, blue, black. Reveals channel/polarity errors.
                if      (x < 240) begin red=255; green=255; blue=255; end
                else if (x < 480) begin red=255; green=255; blue=0; end
                else if (x < 720) begin red=0; green=255; blue=255; end
                else if (x < 960) begin red=0; green=255; blue=0; end
                else if (x < 1200) begin red=255; green=0; blue=255; end
                else if (x < 1440) begin red=255; green=0; blue=0; end
                else if (x < 1680) begin red=0; green=0; blue=255; end
            end else if (y < 900) begin
                red=x[10:3]; green=x[10:3]; blue=x[10:3];
            end else begin
                red = (x[5] ^ y[5]) ? 8'h08 : 8'h18;
                green = (x[5] ^ y[5]) ? 8'hd0 : 8'h18;
                blue = (x[5] ^ y[5]) ? 8'hff : 8'h40;
                if (x == {1'b0,marker}) begin red=255; green=0; blue=255; end
            end
            if (x < 2 || x >= 1918 || y < 2 || y >= 1078) begin
                red=255; green=255; blue=255;
            end
        end
    end
endmodule

module hdmi_raster (
    input clk, reset,
    output reg [11:0] x,
    output reg [10:0] y,
    output active, hs, vs
);
    always @(posedge clk or posedge reset) begin
        if (reset) begin x <= 0; y <= 0; end
        else if (x == 2199) begin
            x <= 0;
            if (y == 1124) begin
                y <= 0;
            end else y <= y + 1'b1;
        end else x <= x + 1'b1;
    end
    assign active = (x < 1920) && (y < 1080);
    assign hs = (x >= 2008) && (x < 2052);
    assign vs = (y >= 1084) && (y < 1089);

endmodule

// DVI 1.0 section 3.2.2: transition minimization, then DC balancing.
// Two pixel pipeline stages; control and DE follow the same pipeline as RGB.
module hdmi_tmds_encoder (
    input clk, reset,
    input [7:0] data,
    input de,
    input [1:0] control,
    output reg [9:0] symbol
);
    reg [3:0] input_ones, qm_ones;
    reg [8:0] qm_next, qm;
    reg use_xnor, de_q;
    reg [1:0] control_q;
    reg signed [5:0] balance_next, balance, disparity;
    integer i;
    always @* begin
        input_ones=0;
        for (i=0; i<8; i=i+1) input_ones=input_ones+data[i];
        use_xnor=(input_ones>4) || ((input_ones==4) && !data[0]);
        qm_next[0]=data[0];
        for (i=1; i<8; i=i+1)
            qm_next[i]=qm_next[i-1]^data[i]^use_xnor;
        qm_next[8]=!use_xnor;
        qm_ones=0;
        for (i=0; i<8; i=i+1) qm_ones=qm_ones+qm_next[i];
        balance_next=$signed({1'b0,qm_ones,1'b0})-6'sd8;
    end
    always @(posedge clk or posedge reset) begin
        if (reset) begin
            qm<=0; balance<=0; de_q<=0; control_q<=0;
            disparity<=0; symbol<=10'b1101010100;
        end else begin
            qm<=qm_next; balance<=balance_next; de_q<=de; control_q<=control;
            if (!de_q) begin
                disparity<=0;
                case (control_q)
                    2'b00: symbol<=10'b1101010100;
                    2'b01: symbol<=10'b0010101011;
                    2'b10: symbol<=10'b0101010100;
                    2'b11: symbol<=10'b1010101011;
                endcase
            end else if ((disparity==0) || (balance==0)) begin
                symbol<={~qm[8],qm[8],(qm[8] ? qm[7:0] : ~qm[7:0])};
                disparity<=disparity+(qm[8] ? balance : -balance);
            end else if (disparity[5]==balance[5]) begin
                symbol<={1'b1,qm[8],~qm[7:0]};
                disparity<=disparity+(qm[8] ? 6'sd2 : 6'sd0)-balance;
            end else begin
                symbol<={1'b0,qm[8],qm[7:0]};
                disparity<=disparity+balance-(qm[8] ? 6'sd0 : 6'sd2);
            end
        end
    end
endmodule

module hdmi_serializer (
    input pixel_clk, serial_clk, reset,
    input [39:0] words,
    output [3:0] serial_out
);
    reg word_toggle;
    reg [2:0] pixel_reset;
    always @(posedge pixel_clk or posedge reset)
        if(reset) pixel_reset<=3'b111;
        else pixel_reset<={pixel_reset[1:0],1'b0};
    always @(posedge pixel_clk or posedge pixel_reset[2])
        if (pixel_reset[2]) word_toggle<=0;
        else word_toggle<=~word_toggle;

    // The clocks are related (5:1, same PLL). Synchronizing the toggle delays
    // the bus capture by 2-3 serial cycles, while words remain stable for 5.
    // No independent per-bit synchronizers: each 40-bit bundle is one pixel.
    reg [1:0] toggle_sync;
    reg toggle_seen;
    reg [2:0] serial_reset;
    // A full serial cycle is needed for reset recovery at 371 MHz.
    // Release on the rising edge; keep recovery and removal paths timed.
    always @(posedge serial_clk or posedge reset)
        if (reset) serial_reset<=3'b111;
        else serial_reset<={serial_reset[1:0],1'b0};
    always @(posedge serial_clk or posedge serial_reset[2])
        if (serial_reset[2]) begin toggle_sync<=0; toggle_seen<=0; end
        else begin
            toggle_sync<={toggle_sync[0],word_toggle};
            toggle_seen<=toggle_sync[1];
        end
    wire load_word=toggle_sync[1]^toggle_seen;
    genvar lane;
    generate for (lane=0; lane<4; lane=lane+1) begin: lanes
        reg [9:0] shift;
        always @(posedge serial_clk or posedge serial_reset[2])
            if (serial_reset[2]) shift<=0;
            else if (load_word) shift<=words[lane*10 +: 10];
            else shift<={2'b00,shift[9:2]};
        // EG_LOGIC_ODDR captures both inputs at the rising edge:
        // d0 on rising, d1 on falling. TMDS transmits bit 0 first.
        EG_LOGIC_ODDR ddr (.q(serial_out[lane]), .clk(serial_clk),
            .d0(shift[0]), .d1(shift[1]), .rst(serial_reset[2]));
    end endgenerate
endmodule
