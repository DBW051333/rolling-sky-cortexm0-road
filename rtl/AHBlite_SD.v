// Read-oriented SPI byte engine. Firmware handles SD commands and FAT32.
// DATA 0x00: write launches byte, read {busy,rx}; CS 0x04; divider 0x08.
module AHBlite_SD(
    input HCLK,HRESETn,HSEL,HWRITE,HREADY,
    input [31:0] HADDR,HWDATA,
    input [1:0] HTRANS,
    output reg [31:0] HRDATA,
    output reg SD_CLK,SD_CS,SD_MOSI,
    input SD_MISO
);
    reg [1:0] address;
    reg pending,busy;
    reg [7:0] divider,ticks,tx,rx;
    reg [2:0] bit_count;
    always @(posedge HCLK or negedge HRESETn) begin
        if(!HRESETn) begin
            address<=0; pending<=0; busy<=0; divider<=63; ticks<=0;
            tx<=255; rx<=255; bit_count<=0;
            SD_CLK<=0; SD_CS<=1; SD_MOSI<=1;
        end else begin
            if(busy) begin
                if(ticks==divider) begin
                    ticks<=0;
                    SD_CLK<=~SD_CLK;
                    if(!SD_CLK) rx<={rx[6:0],SD_MISO};
                    else if(bit_count==7) begin
                        busy<=0; SD_MOSI<=1;
                    end else begin
                        bit_count<=bit_count+1'b1;
                        tx<={tx[6:0],1'b1}; SD_MOSI<=tx[6];
                    end
                end else ticks<=ticks+1'b1;
            end
            if(HREADY) begin
                pending<=HSEL && HTRANS[1] && HWRITE;
                if(HSEL && HTRANS[1]) address<=HADDR[3:2];
                if(pending && !busy) case(address)
                    0: begin
                        tx<=HWDATA[7:0]; SD_MOSI<=HWDATA[7];
                        SD_CLK<=0; busy<=1; ticks<=0; bit_count<=0;
                    end
                    1: SD_CS<=HWDATA[0];
                    2: divider<=HWDATA[7:0];
                    default: ;
                endcase
            end
        end
    end
    always @* case(address)
        0: HRDATA={23'b0,busy,rx};
        1: HRDATA={31'b0,SD_CS};
        2: HRDATA={24'b0,divider};
        default: HRDATA=0;
    endcase
endmodule
