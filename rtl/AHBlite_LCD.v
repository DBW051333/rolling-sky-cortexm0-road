// GPIO-compatible LCD plus constant-color spans; no RAM or framebuffer.
// 0x80=color, 0x84=launch N pixels, 0x88=bit0 busy/bit1 error (write 2 clears).
// Each pixel: WR high setup, 2 clocks low, 2 high. Busy writes are rejected.
module AHBlite_LCD(
    input HCLK,HRESETn,HSEL,HWRITE,HREADY,
    input [31:0] HADDR,HWDATA,
    input [1:0] HTRANS,
    input [2:0] HSIZE,
    input [3:0] HPROT,
    output HREADYOUT,HRESP,
    output reg [31:0] HRDATA,
    output LCD_CS,LCD_RS,LCD_WR,LCD_RD,LCD_RST,LCD_BL_CTR,
    output [15:0] LCD_DATA,
    output reg LCD_MODE,
    output LCD_INI_en
);
    reg [5:0] control;
    reg [5:0] address;
    reg write_pending,busy,error;
    reg [15:0] manual_data,span_color,remaining;
    reg [2:0] phase;
    assign LCD_CS=busy ? 1'b0:control[0];
    assign LCD_RS=busy ? 1'b1:control[1];
    assign LCD_WR=busy ? !(phase==1 || phase==2):control[2];
    assign LCD_RD=busy ? 1'b1:control[3];
    assign LCD_RST=control[4];
    assign LCD_BL_CTR=control[5];
    assign LCD_DATA=busy ? span_color:manual_data;
    assign HREADYOUT=1;
    assign HRESP=0;
    assign LCD_INI_en=0;
    always @(posedge HCLK or negedge HRESETn) begin
        if (!HRESETn) begin
            control<=6'b001111; manual_data<=0; LCD_MODE<=1;
            address<=0; write_pending<=0;
            busy<=0; error<=0; span_color<=0; remaining<=0; phase<=0;
        end else begin
            if(busy) begin
                if(phase==4) begin
                    phase<=0;
                    if(remaining==1) begin busy<=0; remaining<=0; end
                    else remaining<=remaining-1'b1;
                end else phase<=phase+1'b1;
            end
            if(HREADY) begin
                write_pending<=HSEL && HTRANS[1] && HWRITE && HSIZE==3'b010;
                if(HSEL && HTRANS[1]) address<=HADDR[7:2];
                if(write_pending) begin
                    if(address==34) begin if(HWDATA[1]) error<=0; end
                    else if(busy) error<=1;
                    else if(address==32) span_color<=HWDATA[15:0];
                    else if(address==33) begin
                        if(HWDATA[31:16]!=0) error<=1;
                        else if(HWDATA[15:0]!=0) begin
                            remaining<=HWDATA[15:0]; phase<=0; busy<=1;
                        end
                    end
                    else if(address<6) control[address]<=HWDATA[0];
                    else if(address<22) manual_data[address-6]<=HWDATA[0];
                    else if(address==22) LCD_MODE<=HWDATA[0];
                    else if(address==24) manual_data<=HWDATA[15:0];
                end
            end
        end
    end
    always @* begin
        HRDATA=0;
        if(address<6) HRDATA={31'b0,control[address]};
        else if(address<22) HRDATA={31'b0,manual_data[address-6]};
        else if(address==24) HRDATA={16'b0,manual_data};
        else if(address==32) HRDATA={16'b0,span_color};
        else if(address==33) HRDATA={16'b0,remaining};
        else if(address==34) HRDATA={30'b0,error,busy};
    end
endmodule
