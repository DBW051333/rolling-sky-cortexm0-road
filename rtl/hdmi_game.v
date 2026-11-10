// Native portrait display, autonomous RLE background and alpha UI layers.
// Two RGB332 background buffers, two RGBA332 UI buffers, 4 MiB compressed ring.
module hdmi_game #(parameter CLEAR_QUADS=131072) (
    input soc_clk,pixel_clk,serial_clk,reset,
    input write_en,
    input [20:0] write_addr,
    input [8:0] write_data,
    input flush,
    output ready,write_ready,idle,
    output [3:0] hdmi_p,
    input [7:0] movie_address,
    input movie_write,
    input [31:0] movie_data,
    output [31:0] movie_result
);
    reg cleared;
    reg [1:0] clear_sync;
    wire cache_initialized,cache_ready,cache_idle;
    wire ui_initialized,ui_ready,ui_idle;
    wire [1:0] posted,ui_posted;
    reg [1:0] acknowledged,ui_acknowledged,post_sync0,post_sync1,ui_post_sync0,ui_post_sync1;
    wire [7:0] band0,band1,ui_band0,ui_band1;
    wire [11:0] cache_read_address,ui_read_address;
    wire [31:0] cache_read_data,ui_read_data;
    wire [3:0] cache_read_mask,ui_read_mask;
    wire movie_enabled,overlay_enabled,movie_owner,movie_pixel_valid,movie_back,movie_cache_flush;
    wire [18:0] movie_pixel_address;
    wire [7:0] movie_pixel_data;
    wire upload_request,compressed_request,present_request,present_buffer,ui_request,ui_buffer;
    wire [21:0] upload_address,compressed_address;
    wire [127:0] upload_data;
    reg upload_acknowledge,compressed_acknowledge,present_acknowledge,ui_present_ack;
    reg [127:0] compressed_data;
    reg [1:0] upload_sync,compressed_sync,present_sync,ui_present_sync,enabled_sync,overlay_sync;
    reg front,ui_front,mode_latched,overlay_latched;
    menu_movie movie(.clk(soc_clk),.reset(reset),.register_address(movie_address),
        .register_write(movie_write),.register_data(movie_data),.register_result(movie_result),
        .enabled(movie_enabled),.overlay_enabled(overlay_enabled),.owner(movie_owner),
        .pixel_valid(movie_pixel_valid),.pixel_address(movie_pixel_address),.pixel_data(movie_pixel_data),
        .back_buffer(movie_back),.pixel_ready(ready && cache_ready),.cache_idle(cache_idle),
        .cache_flush(movie_cache_flush),.upload_request(upload_request),.upload_address(upload_address),
        .upload_data(upload_data),.upload_acknowledge(upload_acknowledge),
        .read_request(compressed_request),.read_address(compressed_address),
        .read_acknowledge(compressed_acknowledge),.read_data(compressed_data),
        .present_request(present_request),.present_buffer(present_buffer),.present_acknowledge(present_acknowledge),
        .ui_request(ui_request),.ui_buffer(ui_buffer),.ui_acknowledge(ui_present_ack));
    always @(posedge soc_clk or posedge reset)
        if(reset)clear_sync<=0;else clear_sync<={clear_sync[0],cleared};
    always @(posedge pixel_clk or posedge reset)
        if(reset) begin
            post_sync0<=0;post_sync1<=0;ui_post_sync0<=0;ui_post_sync1<=0;
            upload_sync<=0;compressed_sync<=0;present_sync<=0;ui_present_sync<=0;
            enabled_sync<=0;overlay_sync<=0;
        end else begin
            post_sync0<=posted;post_sync1<=post_sync0;
            ui_post_sync0<=ui_posted;ui_post_sync1<=ui_post_sync0;
            upload_sync<={upload_sync[0],upload_request};
            compressed_sync<={compressed_sync[0],compressed_request};
            present_sync<={present_sync[0],present_request};
            ui_present_sync<={ui_present_sync[0],ui_request};
            enabled_sync<={enabled_sync[0],movie_enabled};
            overlay_sync<={overlay_sync[0],overlay_enabled};
        end
    assign ready=clear_sync[1] && cache_initialized && ui_initialized;
    assign write_ready=ready && (write_addr[20] ? ui_ready : !movie_owner && cache_ready);
    assign idle=write_addr[20] ? ui_idle : !movie_owner && cache_idle;
    video_band_cache cache(.soc_clk(soc_clk),.memory_clk(pixel_clk),.reset(reset),
        .pixel_valid(ready && (movie_owner ? movie_pixel_valid : write_en && !write_addr[20])),
        .pixel_x(movie_owner ? movie_pixel_address[18:10] : write_addr[18:10]),
        .pixel_y(movie_owner ? movie_pixel_address[9:0] : write_addr[9:0]),
        .pixel_data(movie_owner ? {1'b1,movie_pixel_data} : write_data),
        .pixel_target(movie_owner ? {1'b0,movie_back} : write_addr[20:19]),.pixel_ready(cache_ready),
        .flush(movie_owner ? movie_cache_flush : flush && !write_addr[20]),
        .idle(cache_idle),.initialized(cache_initialized),.posted(posted),.band0(band0),.band1(band1),
        .acknowledged(acknowledged),.read_address(cache_read_address),.read_data(cache_read_data),.read_mask(cache_read_mask));
    video_band_cache #(.SINGLE_BANK(1),.DATA_IMPLEMENT("9K"),.PIXEL16(1)) ui_cache(
        .soc_clk(soc_clk),.memory_clk(pixel_clk),.reset(reset),
        .pixel_valid(ready && write_en && write_addr[20]),.pixel_x(write_addr[18:10]),
        .pixel_y(write_addr[9:0]),.pixel_data(write_data),.pixel_target(write_addr[20:19]),.pixel_ready(ui_ready),
        .flush(flush && write_addr[20]),.idle(ui_idle),.initialized(ui_initialized),
        .posted(ui_posted),.band0(ui_band0),.band1(ui_band1),.acknowledged(ui_acknowledged),
        .read_address(ui_read_address),.read_data(ui_read_data),.read_mask(ui_read_mask));

    wire [11:0] x;
    wire [10:0] y;
    wire active,hs,vs;
    hdmi_raster raster(pixel_clk,reset,x,y,active,hs,vs);
    reg [31:0] lines [0:511];
    reg [31:0] ui_lines [0:1023];
    reg [8:0] source_line,target_line;
    reg [1:0] repeat_y,repeat_x;
    reg [9:0] source_column;
    reg [8:0] tag0,tag1;
    reg valid0,valid1,fetch,fetch_layer,fetch_ui;
    reg [6:0] fetch_quad;
    reg [15:0] fetch_base;
    reg [31:0] scanned_word,scanned_ui;
    reg [1:0] byte_select;
    reg half_select;
    reg active_d,hs_d,vs_d,image_d;
    wire line_valid=source_line[0] ? valid1 && tag1==source_line : valid0 && tag0==source_line;
    always @(posedge pixel_clk) begin
        scanned_word<=lines[{source_line[0],source_column[9:2]}];
        scanned_ui<=ui_lines[{source_line[0],source_column[9:1]}];
        byte_select<=source_column[1:0];half_select<=source_column[0];
    end
    wire [15:0] ui_pixel=scanned_ui>>(half_select*16);
    wire [7:0] pixel=overlay_latched && ui_pixel[8] ? ui_pixel[7:0] : scanned_word>>(byte_select*8);
    wire [7:0] red=image_d ? {pixel[7:5],pixel[7:5],pixel[7:6]} : 0;
    wire [7:0] green=image_d ? {pixel[4:2],pixel[4:2],pixel[4:3]} : 0;
    wire [7:0] blue=image_d ? {4{pixel[1:0]}} : 0;
    hdmi_tmds_tx tx(pixel_clk,serial_clk,reset,red,green,blue,active_d,hs_d,vs_d,hdmi_p);

    localparam IDLE=0,PRELOAD=1,ISSUE=2,WAIT_MEMORY=3,STORE=4;
    reg [2:0] state,operation;
    reg flush_active,flush_bank,flush_ui,ui_half;
    reg [7:0] flush_band;
    reg [8:0] flush_column;
    reg [2:0] preload_count;
    reg [1:0] store_beat;
    reg [16:0] clear_quad;
    reg [20:0] burst_address;
    reg [127:0] burst_data,scan_data;
    reg [15:0] burst_mask;
    wire mem_ready,mem_done;
    wire [127:0] mem_result;
    wire [1:0] pending=post_sync1^acknowledged;
    wire [1:0] ui_pending=ui_post_sync1^ui_acknowledged;
    assign cache_read_address={flush_bank,flush_column,preload_count[1:0]};
    assign ui_read_address={flush_column,ui_half,preload_count[1:0]};
    wire [31:0] load_data=flush_ui ? ui_read_data : cache_read_data;
    wire [3:0] load_mask=flush_ui ? ui_read_mask : cache_read_mask;
    sdram_word_port memory(.clk(pixel_clk),.reset(reset),.request(state==ISSUE),
        .write(operation==1 || operation==2 || operation==3 || operation==4),.address(burst_address),
        .data(burst_data),.mask(burst_mask),.ready(mem_ready),.done(mem_done),.result(mem_result));
    task finish_column;
        begin
            if(flush_ui && !ui_half) ui_half<=1;
            else begin
                ui_half<=0;
                if(flush_column==359) begin
                    if(flush_ui)ui_acknowledged[0]<=ui_post_sync1[0];
                    else acknowledged[flush_bank]<=post_sync1[flush_bank];
                    flush_active<=0;
                end else flush_column<=flush_column+1'b1;
            end
        end
    endtask
    always @(posedge pixel_clk or posedge reset) begin
        if(reset) begin
            source_line<=0;target_line<=0;repeat_y<=0;repeat_x<=0;source_column<=0;
            tag0<=0;tag1<=0;valid0<=0;valid1<=0;fetch<=0;fetch_quad<=0;fetch_base<=0;
            fetch_layer<=0;fetch_ui<=0;active_d<=0;hs_d<=0;vs_d<=0;image_d<=0;
            front<=0;ui_front<=0;mode_latched<=0;overlay_latched<=0;present_acknowledge<=0;ui_present_ack<=0;
            cleared<=0;acknowledged<=0;ui_acknowledged<=0;state<=IDLE;operation<=0;
            flush_active<=0;flush_bank<=0;flush_ui<=0;ui_half<=0;flush_band<=0;flush_column<=0;
            preload_count<=0;store_beat<=0;clear_quad<=0;burst_address<=0;burst_data<=0;
            burst_mask<=0;scan_data<=0;upload_acknowledge<=0;compressed_acknowledge<=0;compressed_data<=0;
        end else begin
            active_d<=active;hs_d<=hs;vs_d<=vs;image_d<=active && cleared && line_valid;
            if(active && x<1919) begin
                if(repeat_x==2)begin repeat_x<=0;source_column<=source_column+1'b1;end
                else repeat_x<=repeat_x+1'b1;
            end else begin source_column<=0;repeat_x<=0;end
            if(x==2199) begin
                if(y==1124)begin source_line<=0;repeat_y<=0;end
                else if(y<1079)begin
                    if(repeat_y==2)begin repeat_y<=0;source_line<=source_line+1'b1;end
                    else repeat_y<=repeat_y+1'b1;
                end
            end
            if(x==0 && y==1080)begin
                mode_latched<=enabled_sync[1];overlay_latched<=overlay_sync[1];
                if(present_sync[1]!=present_acknowledge)begin
                    front<=present_buffer;present_acknowledge<=present_sync[1];
                end
                if(ui_present_sync[1]!=ui_present_ack)begin
                    ui_front<=ui_buffer;ui_present_ack<=ui_present_sync[1];
                end
            end
            if(cleared && !fetch && x==0 &&
               ((y==1080) || (active && repeat_y==0 && source_line<359))) begin
                target_line<=y==1080?0:source_line+1'b1;
                fetch_base<=y==1080?0:(source_line+1'b1)*160;
                fetch_quad<=0;fetch_layer<=0;fetch_ui<=y==1080?overlay_sync[1]:overlay_latched;fetch<=1;
            end
            case(state)
                IDLE: begin
                    preload_count<=0;
                    if(!cleared)begin
                        operation<=2;burst_address<={2'd0,clear_quad,2'b00};
                        burst_data<=0;burst_mask<=0;state<=ISSUE;
                    end else if(fetch)begin
                        operation<=fetch_layer?6:0;
                        if(fetch_layer)burst_address<=(ui_front?21'h40000:21'h20000)+({5'd0,fetch_base}<<1)+{fetch_quad,2'b00};
                        else burst_address<=((mode_latched && front)?21'h10000:0)+fetch_base+{fetch_quad,2'b00};
                        burst_mask<=0;state<=ISSUE;
                    end else if(compressed_sync[1]!=compressed_acknowledge)begin
                        operation<=5;burst_address<=21'h80000+{1'b0,compressed_address[21:2]};
                        burst_mask<=0;state<=ISSUE;
                    end else if(upload_sync[1]!=upload_acknowledge)begin
                        operation<=4;burst_address<=21'h80000+{1'b0,upload_address[21:2]};
                        burst_data<=upload_data;burst_mask<=0;state<=ISSUE;
                    end else if(flush_active)begin operation<=flush_ui?3:1;state<=PRELOAD;end
                    else if(pending!=0)begin
                        flush_bank<=!pending[0];flush_band<=pending[0]?band0:band1;
                        flush_column<=0;flush_ui<=0;ui_half<=0;flush_active<=1;operation<=1;state<=PRELOAD;
                    end else if(ui_pending[0])begin
                        flush_bank<=0;flush_band<=ui_band0;flush_column<=0;
                        flush_ui<=1;ui_half<=0;flush_active<=1;operation<=3;state<=PRELOAD;
                    end
                end
                PRELOAD:begin
                    preload_count<=preload_count+1'b1;
                    case(preload_count)
                        1:begin burst_data[31:0]<=load_data;burst_mask[3:0]<=load_mask;end
                        2:begin burst_data[63:32]<=load_data;burst_mask[7:4]<=load_mask;end
                        3:begin burst_data[95:64]<=load_data;burst_mask[11:8]<=load_mask;end
                        4:begin
                            burst_data[127:96]<=load_data;burst_mask[15:12]<=load_mask;
                            if(flush_ui)burst_address<=(flush_band[6]?21'h40000:21'h20000)+flush_column*320+312-{flush_band[5:0],3'd0}+{ui_half,2'd0};
                            else burst_address<=(flush_band[6]?21'h10000:0)+flush_column*160+156-{flush_band[5:0],2'd0};
                            if({load_mask,burst_mask[11:0]}==16'hffff)begin finish_column();state<=IDLE;end
                            else state<=ISSUE;
                        end
                    endcase
                end
                ISSUE:if(mem_ready)state<=WAIT_MEMORY;
                WAIT_MEMORY:if(mem_done)begin
                    if(operation==0 || operation==6)begin scan_data<=mem_result;store_beat<=0;state<=STORE;end
                    else begin
                        state<=IDLE;
                        case(operation)
                            1,3:finish_column();
                            2:if(clear_quad==CLEAR_QUADS-1)cleared<=1;else clear_quad<=clear_quad+1'b1;
                            4:upload_acknowledge<=upload_sync[1];
                            5:begin compressed_data<=mem_result;compressed_acknowledge<=compressed_sync[1];end
                        endcase
                    end
                end
                STORE:begin
                    if(operation==6)ui_lines[{target_line[0],fetch_quad,store_beat}]<=scan_data[store_beat*32+:32];
                    else lines[{target_line[0],fetch_quad[5:0],store_beat}]<=scan_data[store_beat*32+:32];
                    if(store_beat==3)begin
                        state<=IDLE;
                        if(fetch_quad==(fetch_layer?79:39))begin
                            if(!fetch_layer && fetch_ui)begin fetch_layer<=1;fetch_quad<=0;end
                            else begin
                                fetch<=0;
                                if(target_line[0])begin valid1<=1;tag1<=target_line;end
                                else begin valid0<=1;tag0<=target_line;end
                            end
                        end else fetch_quad<=fetch_quad+1'b1;
                    end else store_beat<=store_beat+1'b1;
                end
                default:state<=IDLE;
            endcase
        end
    end
endmodule
