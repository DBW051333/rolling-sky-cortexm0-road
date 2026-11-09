// Direct RGB332 framebuffer writes from the AHB video peripheral.
// HDMI scans the active viewport of a 240-byte-stride framebuffer.
module hdmi_game (
    input soc_clk, pixel_clk, serial_clk, reset,
    input write_en,
    input [16:0] write_addr,
    input [7:0] write_data,
    output ready,
    output [3:0] hdmi_p
);
    reg [2:0] pixel_reset, soc_reset;
    always @(posedge pixel_clk or posedge reset)
        if(reset) pixel_reset<=3'b111;
        else pixel_reset<={pixel_reset[1:0],1'b0};
    always @(posedge soc_clk or posedge reset)
        if(reset) soc_reset<=3'b111;
        else soc_reset<={soc_reset[1:0],1'b0};

    // Scrub uninitialized RAM before accepting writes from the CPU.
    reg [16:0] clear_addr;
    reg cleared;
    always @(posedge soc_clk or posedge soc_reset[2])
        if(soc_reset[2]) begin clear_addr<=0; cleared<=0; end
        else if(!cleared) begin
            if(clear_addr==76799) cleared<=1;
            else clear_addr<=clear_addr+1'b1;
        end
    assign ready=cleared;
    wire [16:0] read_addr;
    wire [7:0] read_data;
    hdmi_framebuffer framebuffer (.write_clk(soc_clk), .read_clk(pixel_clk),
        .write_en(!cleared || write_en),
        .write_addr(cleared ? write_addr : clear_addr),
        .write_data(cleared ? write_data : 8'd0),
        .read_addr(read_addr), .read_data(read_data));
    reg [1:0] cleared_sync;
    always @(posedge pixel_clk or posedge pixel_reset[2])
        if(pixel_reset[2]) cleared_sync<=0;
        else cleared_sync<={cleared_sync[0],cleared};

    wire [11:0] x;
    wire [10:0] y;
    wire active,hs,vs;
    wire [7:0] red,green,blue;
    wire active_d,hs_d,vs_d;
    hdmi_raster raster(pixel_clk,pixel_reset[2],x,y,active,hs,vs);
    hdmi_game_scaler scaler (.clk(pixel_clk), .reset(pixel_reset[2]),
        .x(x), .y(y), .active(active), .hs(hs), .vs(vs), .ready(cleared_sync[1]),
        .read_addr(read_addr), .read_data(read_data),
        .red(red), .green(green), .blue(blue),
        .active_d(active_d), .hs_d(hs_d), .vs_d(vs_d));
    hdmi_tmds_tx tx (.pixel_clk(pixel_clk), .serial_clk(serial_clk),
        .reset(reset), .red(red), .green(green), .blue(blue),
        .active(active_d), .hs(hs_d), .vs(vs_d), .hdmi_p(hdmi_p));
endmodule

// Monitor is physically rotated 90 degrees counterclockwise. Rotate the
// Native 1920x1080 signal, physically upright 1080x1920 after rotation.
// Compose 198x320 pixels in columns 21..218 of the 240-byte stride.
// User-estimated monitor compensation: 60/11 horizontal, 6x vertical scaling.
// Source = (21+floor(y*11/60), floor((1919-x)/6)); no dividers/DSPs.
module hdmi_game_scaler (
    input clk,reset,
    input [11:0] x,
    input [10:0] y,
    input active,hs,vs,ready,
    output [16:0] read_addr,
    input [7:0] read_data,
    output [7:0] red,green,blue,
    output reg active_d,hs_d,vs_d
);
    reg [7:0] source_x;
    reg [2:0] fraction_x;
    reg [5:0] fraction_y;
    reg [16:0] row_address;
    reg image_d;
    wire image=active;
    assign read_addr=image ? row_address+{9'd0,source_x} : 17'd0;
    always @(posedge clk or posedge reset) begin
        if(reset) begin
            source_x<=21; fraction_x<=5; fraction_y<=0; row_address<=17'd76560;
            active_d<=0; hs_d<=0; vs_d<=0; image_d<=0;
        end else begin
            active_d<=active; hs_d<=hs; vs_d<=vs; image_d<=image && ready;
            // Scan source rows bottom-to-top along each output scanline.
            // 319*240=76560; initial remainder is 1919 % 6 = 5.
            if(image && x<1919) begin
                if(fraction_x==0) begin row_address<=row_address-17'd240; fraction_x<=5; end
                else fraction_x<=fraction_x-1'b1;
            end else begin row_address<=17'd76560; fraction_x<=5; end
            if(x==2199) begin
                if(y==1124) begin source_x<=21; fraction_y<=0; end
                else if(y<1079) begin
                    // Source columns progress left-to-right across output lines.
                    if(fraction_y>=49) begin source_x<=source_x+1'b1; fraction_y<=fraction_y-6'd49; end
                    else fraction_y<=fraction_y+6'd11;
                end
            end
        end
    end
    // Read RAM has one-cycle latency; image/DE/HS/VS are delayed identically.
    assign red=image_d ? {read_data[7:5],read_data[7:5],read_data[7:6]} : 8'd0;
    assign green=image_d ? {read_data[4:2],read_data[4:2],read_data[4:3]} : 8'd0;
    assign blue=image_d ? {4{read_data[1:0]}} : 8'd0;
endmodule

// Explicit heterogeneous banking keeps the framebuffer within both BRAM pools:
// 8 x 4096-byte BRAM32K + 43 x 1024-byte BRAM9K = 76800 bytes.
// Existing CPU uses another 8 BRAM32K and 8 BRAM9K: total 16/16 and 51/64.
module hdmi_framebuffer (
    input write_clk,read_clk,write_en,
    input [16:0] write_addr,read_addr,
    input [7:0] write_data,
    output [7:0] read_data
);
    wire [407:0] bank_data;
    reg [5:0] read_bank;
    always @(posedge read_clk)
        read_bank<=(read_addr<32768) ? {3'b000,read_addr[14:12]} : (read_addr[16:10]-6'd24);
    assign read_data=(read_bank<51) ? bank_data[read_bank*8 +: 8] : 8'd0;
    genvar bank;
    generate for(bank=0;bank<51;bank=bank+1) begin: banks
        if(bank<8) begin: ram32k
            hdmi_ram_bank #(.ADDRESS_BITS(12),.IMPLEMENT("32K")) ram (
                .wclk(write_clk), .rclk(read_clk),
                .we(write_en && write_addr<32768 && write_addr[14:12]==bank),
                .waddr(write_addr[11:0]), .raddr(read_addr[11:0]),
                .data(write_data), .q(bank_data[bank*8 +: 8]));
        end else begin: ram9k
            hdmi_ram_bank #(.ADDRESS_BITS(10),.IMPLEMENT("9K")) ram (
                .wclk(write_clk), .rclk(read_clk),
                .we(write_en && write_addr[16:10]==bank+24),
                .waddr(write_addr[9:0]), .raddr(read_addr[9:0]),
                .data(write_data), .q(bank_data[bank*8 +: 8]));
        end
    end endgenerate
endmodule

module hdmi_ram_bank #(parameter ADDRESS_BITS=10, parameter IMPLEMENT="9K") (
    input wclk,rclk,we,
    input [ADDRESS_BITS-1:0] waddr,raddr,
    input [7:0] data,
    output [7:0] q
);
`ifdef HDMI_SIM_RAM
    // Icarus cannot read the vendor's encrypted RAM model. Match its NOREG
    // synchronous read semantics; synthesis always uses the native macro.
    reg [7:0] mem [0:(1<<ADDRESS_BITS)-1];
    reg [7:0] read_q;
    always @(posedge wclk) if(we) mem[waddr]<=data;
    always @(posedge rclk) read_q<=mem[raddr];
    assign q=read_q;
`else
    EG_LOGIC_BRAM #(.DATA_WIDTH_A(8),.DATA_WIDTH_B(8),
        .ADDR_WIDTH_A(ADDRESS_BITS),.ADDR_WIDTH_B(ADDRESS_BITS),
        .MODE("DP"),.IMPLEMENT(IMPLEMENT),.REGMODE_A("NOREG"),.REGMODE_B("NOREG")) memory (
        .clka(wclk),.addra(waddr),.dia(data),.wea(we),.cea(1'b1),
        .ocea(1'b1),.rsta(1'b0),.bea(1'b1),.doa(),
        .clkb(rclk),.addrb(raddr),.dib(8'd0),.web(1'b0),.ceb(1'b1),
        .oceb(1'b1),.rstb(1'b0),.beb(1'b1),.dob(q));
`endif
endmodule
