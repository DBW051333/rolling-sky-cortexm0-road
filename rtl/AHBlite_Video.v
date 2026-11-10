// Queued native 360x640 span writer. Register commands are retained in order
// while the execution engine fills the BRAM transpose cache.
// 0x88 status: bit0 queue pressure, bit1 error, bit2 ready, bit3 fully drained.
// 0x90 fences all preceding commands through the SDRAM write acknowledgement.
module AHBlite_Video (
    input HCLK,HRESETn,HSEL,HWRITE,HREADY,
    input [31:0] HADDR,HWDATA,
    input [1:0] HTRANS,
    input [2:0] HSIZE,
    output [31:0] HRDATA,
    input framebuffer_ready,framebuffer_write_ready,framebuffer_idle,
    output write_en,
    output [20:0] write_addr,
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
    reg [1:0] draw_target;
    reg packed_mode;
    reg [31:0] packed_pixels;
    reg [17:0] remaining;
    reg [39:0] commands [0:63];
    reg [6:0] producer,consumer;
    reg flushing;
    reg [1:0] fence_delay;
    wire [6:0] queued=producer-consumer;
    wire pressure=queued>=56 || !framebuffer_ready;
    wire drained=queued==0 && !busy && !flushing && !write_pending && framebuffer_idle;
    wire [39:0] command=commands[consumer[5:0]];
    wire [7:0] opcode=command[39:32];
    wire [31:0] value=command[31:0];
    assign HRDATA=address>=8'hc0 ? movie_result : address==8'h88 ? {28'd0,drained,framebuffer_ready,error|overflow,pressure}:0;
    assign write_en=busy && framebuffer_ready;
    assign write_addr={draw_target,cursor_x,cursor_y};
    assign movie_address=address;
    assign movie_write=write_pending && HREADY && address>=8'hc0;
    assign movie_data=HWDATA;
    assign write_data=packed_mode ? {packed_pixels[15:0]!=0,packed_pixels[15:13],packed_pixels[10:8],packed_pixels[4:3]} : {opaque,color};
    always @(posedge HCLK or negedge HRESETn) begin
        if(!HRESETn) begin
            address<=0;write_pending<=0;overflow<=0;producer<=0;
        end else if(HREADY) begin
            write_pending<=HSEL && HTRANS[1] && HWRITE && HSIZE==3'b010;
            if(HSEL && HTRANS[1]) address<=HADDR[7:0];
            if(write_pending) begin
                if(address==8'h88 && HWDATA[1]) overflow<=0;
                case(address)
                    8'h00,8'h04,8'h08,8'h80,8'h84,8'h88,8'h90,8'ha0,8'hb0:
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
        end else begin
            flush<=0;
            if(write_en && framebuffer_write_ready) begin
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
            end else if(!busy && queued!=0 && framebuffer_ready) begin
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
                    8'h84: begin
                        packed_mode<=0;
                        if(value>230400 || !armed) error<=1;
                        else if(value!=0) begin remaining<=value[17:0];busy<=1;end
                    end
                    8'ha0: begin
                        if(!armed)error<=1;
                        else begin packed_mode<=1;packed_pixels<=value;remaining<=2;busy<=1;end
                    end
                    8'hb0: draw_target<=value[1:0];
                    8'h88: if(value[1]) error<=0;
                    8'h90: begin flush<=1;flushing<=1;fence_delay<=0;end
                endcase
            end
        end
    end
endmodule
