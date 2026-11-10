// Two 16-row native portrait bands, transposed into 32-bit SDRAM words.
// The SoC owns the fill bank; the memory clock owns a posted bank until ACK.
// Metadata and BRAM contents remain stable throughout that ownership transfer.
module video_band_cache #(parameter SINGLE_BANK=0,parameter DATA_IMPLEMENT="32K",parameter PIXEL16=0) (
    input soc_clk, memory_clk, reset,
    input pixel_valid,
    input [8:0] pixel_x,
    input [9:0] pixel_y,
    input [8:0] pixel_data,
    input [1:0] pixel_target,
    output pixel_ready,
    input flush,
    output idle, initialized,
    output reg [1:0] posted,
    output reg [7:0] band0,band1,
    input [1:0] acknowledged,
    input [11:0] read_address,
    output [31:0] read_data,
    output [3:0] read_mask
);
    reg [1:0] ack0_sync,ack1_sync;
    localparam CLEAR=0,FILL=1,SWITCH=2,FENCE=3;
    reg [1:0] state;
    reg fill_bank,dirty,fence_pending,started;
    reg [7:0] fill_band,next_band;
    reg [11:0] clear_address;
    wire [1:0] ack_sync={ack1_sync[1],ack0_sync[1]};
    wire [11:0] pixel_address=PIXEL16 ? {pixel_x,~pixel_y[3:1]} : {fill_bank,pixel_x,~pixel_y[3:2]};
    wire [11:0] clear_index=PIXEL16 ? clear_address : {fill_bank,clear_address[10:0]};
    assign initialized=started;
    assign pixel_ready=state==FILL && !fence_pending && !flush &&
        (!dirty || {pixel_target,pixel_y[9:4]}==fill_band);
    assign idle=state==FILL && !dirty && !fence_pending && !flush && posted==ack_sync;
    localparam ADDRESS_BITS=SINGLE_BANK && !PIXEL16?11:12;
    wire next_bank=SINGLE_BANK?fill_bank:!fill_bank;
    wire pixel_accept=pixel_valid && pixel_ready;
    wire [11:0] mask_address=state==CLEAR ? clear_index : pixel_address;
    genvar lane;
    generate for(lane=0;lane<4;lane=lane+1) begin: lanes
        wire lane_write=pixel_accept && (PIXEL16 ? ((lane/2)==!pixel_y[0]) : (3-pixel_y[1:0])==lane);
        wire [7:0] lane_data=PIXEL16 && (lane%2) ? {7'd0,pixel_data[8]} : pixel_data[7:0];
        wire valid_byte;
        video_cache_ram #(.WIDTH(8),.IMPLEMENT(DATA_IMPLEMENT),.ADDRESS_BITS(ADDRESS_BITS)) data_ram(
            .write_clk(soc_clk),.read_clk(memory_clk),.write_en(lane_write),
            .write_address(pixel_address[ADDRESS_BITS-1:0]),.write_data(lane_data),
            .read_address(read_address[ADDRESS_BITS-1:0]),.read_data(read_data[lane*8 +: 8]));
        video_cache_ram #(.WIDTH(1),.IMPLEMENT("9K"),.ADDRESS_BITS(ADDRESS_BITS)) valid_ram(
            .write_clk(soc_clk),.read_clk(memory_clk),
            .write_en(state==CLEAR || lane_write),.write_address(mask_address[ADDRESS_BITS-1:0]),
            .write_data(state!=CLEAR),.read_address(read_address[ADDRESS_BITS-1:0]),.read_data(valid_byte));
        assign read_mask[lane]=!valid_byte;
    end endgenerate
    always @(posedge soc_clk or posedge reset) begin
        if(reset) begin
            ack0_sync<=0;ack1_sync<=0;posted<=0;band0<=0;band1<=0;
            state<=CLEAR;fill_bank<=0;dirty<=0;fence_pending<=0;
            fill_band<=0;next_band<=0;clear_address<=0;started<=0;
        end else begin
            ack0_sync<={ack0_sync[0],acknowledged[0]};
            ack1_sync<={ack1_sync[0],acknowledged[1]};
            if(flush) fence_pending<=1;
            case(state)
                CLEAR: if(clear_address==(PIXEL16?2879:1439)) begin
                    clear_address<=0;started<=1;state<=FILL;
                end else clear_address<=clear_address+1'b1;
                FILL: begin
                    if(flush || fence_pending) begin
                        if(dirty) begin
                            posted[fill_bank]<=~posted[fill_bank];
                            if(fill_bank) band1<=fill_band;else band0<=fill_band;
                        end
                        dirty<=0;state<=FENCE;
                    end else if(pixel_valid) begin
                        if(dirty && {pixel_target,pixel_y[9:4]}!=fill_band) begin
                            posted[fill_bank]<=~posted[fill_bank];
                            if(fill_bank) band1<=fill_band;else band0<=fill_band;
                            dirty<=0;next_band<={pixel_target,pixel_y[9:4]};state<=SWITCH;
                        end else begin dirty<=1;fill_band<={pixel_target,pixel_y[9:4]};end
                    end
                end
                SWITCH: if(posted[next_bank]==ack_sync[next_bank]) begin
                    fill_bank<=next_bank;fill_band<=next_band;
                    clear_address<=0;state<=CLEAR;
                end
                FENCE: if(posted==ack_sync) begin
                    fill_bank<=0;fill_band<=0;clear_address<=0;
                    fence_pending<=0;state<=CLEAR;
                end
            endcase
        end
    end
endmodule

// Explicit uniform-width RAM ports avoid mixed-width inference in TD 5.6.2.
module video_cache_ram #(parameter WIDTH=8,parameter IMPLEMENT="32K",parameter ADDRESS_BITS=12) (
    input write_clk,read_clk,write_en,
    input [ADDRESS_BITS-1:0] write_address,read_address,
    input [WIDTH-1:0] write_data,
    output [WIDTH-1:0] read_data
);
`ifdef HDMI_SIM_RAM
    reg [WIDTH-1:0] memory [0:(1<<ADDRESS_BITS)-1];
    reg [WIDTH-1:0] q;
    always @(posedge write_clk) if(write_en) memory[write_address]<=write_data;
    always @(posedge read_clk) q<=memory[read_address];
    assign read_data=q;
`else
    EG_LOGIC_BRAM #(.DATA_WIDTH_A(WIDTH),.DATA_WIDTH_B(WIDTH),
        .ADDR_WIDTH_A(ADDRESS_BITS),.ADDR_WIDTH_B(ADDRESS_BITS),.MODE("DP"),.IMPLEMENT(IMPLEMENT),
        .REGMODE_A("NOREG"),.REGMODE_B("NOREG")) memory (
        .clka(write_clk),.addra(write_address),.dia(write_data),.wea(write_en),
        .cea(1'b1),.ocea(1'b1),.rsta(1'b0),.bea(1'b1),.doa(),
        .clkb(read_clk),.addrb(read_address),.dib({WIDTH{1'b0}}),.web(1'b0),
        .ceb(1'b1),.oceb(1'b1),.rstb(1'b0),.beb(1'b1),.dob(read_data));
`endif
endmodule
