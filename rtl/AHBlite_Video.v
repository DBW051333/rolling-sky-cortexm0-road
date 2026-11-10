// Queued native 360x640 span writer. Register commands are retained in order
// while the execution engine fills the BRAM transpose cache.
// 0x88 status: bit0 queue pressure, bit1 error, bit2 ready, bit3 fully drained.
// 0x90 fences all preceding commands through the SDRAM write acknowledgement.
// 0x70 submits {RGB565, count16}; 0x6c also composites the cached sphere.
// 0x74 sets signed {top,left}; 0x78 controls the sphere's red damage flash.
// 0x94/98 upload indexed texels; 0x9c/ac upload eight RGB565 palette entries.
// 0x44/48 upload five {grid,fill} materials. 0x60 projects six road edges
// from {scale,camera}; 0x64 rasterizes their clipped span, with horizontal
// grid in bit 0 and sphere composition in bit 1.
// 0xb4/b8/bc expose free-running clock, pixel stall and queue-pressure cycles.
module AHBlite_Video (
    input HCLK,HRESETn,HSEL,HWRITE,HREADY,
    input [31:0] HADDR,HWDATA,
    input [1:0] HTRANS,
    input [2:0] HSIZE,
    output [31:0] HRDATA,
    input framebuffer_ready,framebuffer_write_ready,framebuffer_idle,
    output write_en,
    output [21:0] write_addr,
    output [8:0] write_data,
    output reg flush,
    output [7:0] movie_address,
    output movie_write,
    output [31:0] movie_data,
    input [31:0] movie_result
);
    reg [7:0] address;
    reg write_pending,overflow,error,busy,armed,x_valid,y_valid;
    reg [8:0] left,right,cursor_x;
    reg [9:0] top,bottom,cursor_y;
    reg [7:0] color;
    reg opaque;
    reg [2:0] draw_target;
    reg packed_mode;
    reg [31:0] packed_pixels;
    reg [17:0] remaining;
    reg [39:0] commands [0:63];
    reg [6:0] producer,consumer;
    reg flushing;
    reg [1:0] fence_delay;
    reg [31:0] clock_count,stall_count,pressure_count;
    reg [12:0] sprite_cursor;
    reg [2:0] palette_cursor;
    reg [15:0] sprite_palette [0:7];
    reg signed [15:0] sprite_left,sprite_top;
    reg sprite_mode,sprite_wait,sprite_red;
    reg [31:0] tile_material [0:4];
    reg [2:0] tile_cursor,project_column;
    reg [15:0] project_scale,project_camera;
    reg signed [33:0] project_product;
    reg signed [15:0] tile_edge [0:5];
    reg project_busy,project_phase,tile_mode,tile_horizontal;
    wire signed [17:0] project_world=$signed({1'b0,project_column,8'd0})-$signed({1'b0,project_camera});
    wire signed [17:0] project_delta=project_product<0 ? -((-project_product)>>>16) : project_product>>>16;
    wire signed [26:0] project_display=project_delta*27'sd233+27'sd128;
    wire [2:0] tile_column=$signed({1'b0,cursor_x})>=tile_edge[4]?4:
        $signed({1'b0,cursor_x})>=tile_edge[3]?3:
        $signed({1'b0,cursor_x})>=tile_edge[2]?2:
        $signed({1'b0,cursor_x})>=tile_edge[1]?1:0;
    wire tile_grid=tile_horizontal || $signed({1'b0,cursor_x})==tile_edge[tile_column];
    wire [15:0] tile_pixel=tile_grid?tile_material[tile_column][31:16]:tile_material[tile_column][15:0];
    wire [9:0] tile_start=tile_edge[0]<0?0:tile_edge[0]>360?360:tile_edge[0];
    wire [9:0] tile_stop=tile_edge[5]<0?0:tile_edge[5]>360?360:tile_edge[5];
    wire signed [16:0] sprite_dx=$signed({1'b0,cursor_x})-sprite_left;
    wire signed [16:0] sprite_dy=$signed({1'b0,cursor_y})-sprite_top;
    wire sprite_inside=sprite_dx>=0 && sprite_dx<116 && sprite_dy>=0 && sprite_dy<128;
    wire [9:0] sprite_numerator={sprite_dx[6:0],3'd0}+10'd7;
    wire [6:0] sprite_source_x=sprite_numerator/10'd15;
    wire [11:0] sprite_source_y={sprite_dy[6:0],4'd0}+sprite_dy[6:0];
    wire [3:0] sprite_ink;
    wire [15:0] sprite_colour=sprite_palette[sprite_ink[2:0]];
    wire sprite_hit=sprite_mode && sprite_inside && sprite_ink[2:0]!=0 && sprite_colour!=0;
    wire [15:0] sprite_pixel=sprite_red?16'hf800:sprite_colour;
    wire sprite_upload=!busy && !project_busy && !flushing && queued!=0 && framebuffer_ready && opcode==8'h98;
    wire [6:0] queued=producer-consumer;
    wire pressure=queued>=56 || !framebuffer_ready;
    wire drained=queued==0 && !busy && !project_busy && !flushing && !write_pending && framebuffer_idle;
    wire [39:0] command=commands[consumer[5:0]];
    wire [7:0] opcode=command[39:32];
    wire [31:0] value=command[31:0];
    assign HRDATA=address>=8'hc0 ? movie_result :
        address==8'h88 ? {28'd0,drained,framebuffer_ready,error|overflow,pressure} :
        address==8'hb4 ? clock_count : address==8'hb8 ? stall_count :
        address==8'hbc ? pressure_count : 0;
    assign write_en=busy && framebuffer_ready && (!sprite_mode || !sprite_wait);
    assign write_addr={draw_target,cursor_x,cursor_y};
    assign movie_address=address;
    assign movie_write=write_pending && HREADY && address>=8'hc0;
    assign movie_data=HWDATA;
    assign write_data=sprite_hit ? {1'b1,sprite_pixel[15:13],sprite_pixel[10:8],sprite_pixel[4:3]} :
        tile_mode ? {tile_pixel!=0,tile_pixel[15:13],tile_pixel[10:8],tile_pixel[4:3]} :
        packed_mode ? {packed_pixels[15:0]!=0,packed_pixels[15:13],packed_pixels[10:8],packed_pixels[4:3]} : {opaque,color};
    video_cache_ram #(.WIDTH(4),.IMPLEMENT("9K"),.ADDRESS_BITS(13)) sprite_texture(
        .write_clk(HCLK),.read_clk(HCLK),.write_en(sprite_upload),
        .write_address(sprite_cursor),.write_data(value[3:0]),
        .read_address({sprite_source_y[11:5],sprite_source_x[5:0]}),.read_data(sprite_ink));
    always @(posedge HCLK or negedge HRESETn)
        if(!HRESETn) begin clock_count<=0;stall_count<=0;pressure_count<=0;end
        else begin
            clock_count<=clock_count+1'b1;
            if(write_en && !framebuffer_write_ready)stall_count<=stall_count+1'b1;
            if(pressure)pressure_count<=pressure_count+1'b1;
        end
    always @(posedge HCLK or negedge HRESETn) begin
        if(!HRESETn) begin
            address<=0;write_pending<=0;overflow<=0;producer<=0;
        end else if(HREADY) begin
            write_pending<=HSEL && HTRANS[1] && HWRITE && HSIZE==3'b010;
            if(HSEL && HTRANS[1]) address<=HADDR[7:0];
            if(write_pending) begin
                if(address==8'h88 && HWDATA[1]) overflow<=0;
                case(address)
                    8'h00,8'h04,8'h08,8'h44,8'h48,8'h60,8'h64,8'h6c,8'h70,8'h74,8'h78,8'h7c,8'h80,8'h84,8'h88,8'h90,
                    8'h94,8'h98,8'h9c,8'ha0,8'hac,8'hb0:
                        if(queued<64) begin
                            commands[producer[5:0]]<={address,HWDATA};
                            producer<=producer+1'b1;
                        end else overflow<=1;
                endcase
            end
        end
    end
    always @(posedge HCLK or negedge HRESETn) begin
        if(!HRESETn) begin
            consumer<=0;error<=0;busy<=0;armed<=0;x_valid<=1;y_valid<=1;
            left<=0;right<=359;top<=0;bottom<=639;cursor_x<=0;cursor_y<=0;
            color<=0;opaque<=0;draw_target<=0;packed_mode<=0;packed_pixels<=0;remaining<=0;flush<=0;flushing<=0;fence_delay<=0;
            sprite_cursor<=0;palette_cursor<=0;sprite_left<=0;sprite_top<=0;
            sprite_mode<=0;sprite_wait<=0;sprite_red<=0;
            tile_cursor<=0;project_column<=0;project_scale<=0;project_camera<=0;project_product<=0;
            project_busy<=0;project_phase<=0;tile_mode<=0;tile_horizontal<=0;
        end else begin
            flush<=0;
            if(project_busy)begin
                project_phase<=~project_phase;
                if(!project_phase)project_product<=project_world*$signed({1'b0,project_scale});
                else begin
                    tile_edge[project_column]<=18'sd180+(project_display>>>8);
                    if(project_column==5)project_busy<=0;
                    else project_column<=project_column+1'b1;
                end
            end
            if(busy && sprite_wait)sprite_wait<=0;
            if(write_en && framebuffer_write_ready) begin
                if(sprite_mode)sprite_wait<=1;
                if(packed_mode) packed_pixels<={16'd0,packed_pixels[31:16]};
                if(remaining==1) begin busy<=0;remaining<=0;end
                else remaining<=remaining-1'b1;
                if(cursor_x==right) begin
                    cursor_x<=left;
                    cursor_y<=cursor_y==bottom ? top : cursor_y+1'b1;
                end else cursor_x<=cursor_x+1'b1;
            end
            if(flushing) begin
                if(fence_delay<2) fence_delay<=fence_delay+1'b1;
                else if(framebuffer_idle) flushing<=0;
            end else if(!busy && !project_busy && queued!=0 && framebuffer_ready) begin
                consumer<=consumer+1'b1;
                case(opcode)
                    8'h00: begin
                        left<=value[8:0];right<=value[24:16];armed<=0;
                        x_valid<=value[15:0]<=value[31:16] && value[31:16]<360;
                    end
                    8'h04: begin
                        top<=value[9:0];bottom<=value[25:16];armed<=0;
                        y_valid<=value[15:0]<=value[31:16] && value[31:16]<640;
                    end
                    8'h08: begin
                        armed<=x_valid && y_valid;
                        if(x_valid && y_valid) begin cursor_x<=left;cursor_y<=top;end
                        else error<=1;
                    end
                    8'h80: begin color<={value[15:13],value[10:8],value[4:3]};opaque<=value[15:0]!=0;end
                    8'h74:begin sprite_left<=value[15:0];sprite_top<=value[31:16];end
                    8'h44:tile_cursor<=value[2:0];
                    8'h48:begin
                        if(tile_cursor<5)begin tile_material[tile_cursor]<=value;tile_cursor<=tile_cursor+1'b1;end
                        else error<=1;
                    end
                    8'h60:begin
                        project_scale<=value[31:16];project_camera<=value[15:0];
                        project_column<=0;project_phase<=0;project_busy<=1;
                    end
                    8'h64:begin
                        tile_mode<=1;tile_horizontal<=value[0];sprite_mode<=value[1];sprite_wait<=1;packed_mode<=0;
                        if(!armed)error<=1;
                        else if(tile_stop>tile_start)begin remaining<=tile_stop-tile_start;busy<=1;end
                    end
                    8'h78:sprite_red<=value[0];
                    8'h94:sprite_cursor<=value[12:0];
                    8'h98:sprite_cursor<=sprite_cursor+1'b1;
                    8'h9c:palette_cursor<=value[2:0];
                    8'hac:begin sprite_palette[palette_cursor]<=value[15:0];palette_cursor<=palette_cursor+1'b1;end
                    8'h70,8'h6c:begin
                        tile_mode<=0;
                        color<={value[31:29],value[26:24],value[20:19]};opaque<=value[31:16]!=0;
                        packed_mode<=0;sprite_mode<=opcode==8'h6c;sprite_wait<=1;
                        if(!armed)error<=1;
                        else if(value[15:0]!=0)begin remaining<={2'd0,value[15:0]};busy<=1;end
                    end
                    8'h84,8'h7c: begin
                        tile_mode<=0;
                        packed_mode<=0;sprite_mode<=opcode==8'h7c;sprite_wait<=1;
                        if(value>230400 || !armed) error<=1;
                        else if(value!=0) begin remaining<=value[17:0];busy<=1;end
                    end
                    8'ha0: begin
                        tile_mode<=0;
                        sprite_mode<=0;
                        if(!armed)error<=1;
                        else begin packed_mode<=1;packed_pixels<=value;remaining<=2;busy<=1;end
                    end
                    8'hb0: if(value[2:0]<=4)draw_target<=value[2:0];else error<=1;
                    8'h88: if(value[1]) error<=0;
                    8'h90: begin flush<=1;flushing<=1;fence_delay<=0;end
                endcase
            end
        end
    end
endmodule
