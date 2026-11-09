// Direct 240x320 RGB332 framebuffer span writer, one pixel per SoC clock.
// 0x00={right,left}, 0x04={bottom,top}, 0x08=begin window;
// 0x80=RGB565 color, 0x84=pixel count (0..76800), 0x88=status:
// bit0 busy, bit1 sticky error (W1C), bit2 framebuffer ready.
module AHBlite_Video (
    input HCLK,HRESETn,HSEL,HWRITE,HREADY,
    input [31:0] HADDR,HWDATA,
    input [1:0] HTRANS,
    input [2:0] HSIZE,
    output [31:0] HRDATA,
    input framebuffer_ready,
    output write_en,
    output [16:0] write_addr,
    output [7:0] write_data
);
    reg [7:0] address;
    reg write_pending,busy,error,armed,x_valid,y_valid;
    reg [7:0] left,right,cursor_x,color;
    reg [8:0] top,bottom,cursor_y;
    reg [16:0] row_address,first_row_address,remaining;
    wire [16:0] top_address=({8'd0,top}<<8)-({8'd0,top}<<4);
    assign HRDATA=(address==8'h88) ? {29'd0,framebuffer_ready,error,busy} : 32'd0;
    assign write_en=busy && framebuffer_ready;
    assign write_addr=row_address+{9'd0,cursor_x};
    assign write_data=color;
    always @(posedge HCLK or negedge HRESETn) begin
        if(!HRESETn) begin
            address<=0; write_pending<=0; busy<=0; error<=0; armed<=0;
            left<=0; right<=239; top<=0; bottom<=319; x_valid<=1; y_valid<=1;
            cursor_x<=0; cursor_y<=0; color<=0;
            row_address<=0; first_row_address<=0; remaining<=0;
        end else begin
            if(write_en) begin
                if(remaining==1) begin busy<=0; remaining<=0; end
                else remaining<=remaining-1'b1;
                if(cursor_x==right) begin
                    cursor_x<=left;
                    if(cursor_y==bottom) begin cursor_y<=top; row_address<=first_row_address; end
                    else begin cursor_y<=cursor_y+1'b1; row_address<=row_address+17'd240; end
                end else cursor_x<=cursor_x+1'b1;
            end
            if(HREADY) begin
                write_pending<=HSEL && HTRANS[1] && HWRITE && HSIZE==3'b010;
                if(HSEL && HTRANS[1]) address<=HADDR[7:0];
                if(write_pending) begin
                    if(address==8'h88) begin if(HWDATA[1]) error<=0; end
                    else if(busy) error<=1;
                    else case(address)
                        8'h00: begin
                            left<=HWDATA[7:0]; right<=HWDATA[23:16]; armed<=0;
                            x_valid<=HWDATA[15:0]<=HWDATA[31:16] && HWDATA[31:16]<240;
                        end
                        8'h04: begin
                            top<=HWDATA[8:0]; bottom<=HWDATA[24:16]; armed<=0;
                            y_valid<=HWDATA[15:0]<=HWDATA[31:16] && HWDATA[31:16]<320;
                        end
                        8'h08: begin
                            armed<=x_valid && y_valid && framebuffer_ready;
                            if(x_valid && y_valid && framebuffer_ready) begin
                                cursor_x<=left; cursor_y<=top;
                                row_address<=top_address; first_row_address<=top_address;
                            end else error<=1;
                        end
                        8'h80: color<={HWDATA[15:13],HWDATA[10:8],HWDATA[4:3]};
                        8'h84: begin
                            if(HWDATA>76800 || !armed || !framebuffer_ready) error<=1;
                            else if(HWDATA!=0) begin remaining<=HWDATA[16:0]; busy<=1; end
                        end
                    endcase
                end
            end
        end
    end
endmodule
