`timescale 1ns/1ps
// Independent SDRAM protocol model for the native BL4 path. This verifies
// command order, byte masks, CL3 data timing, row/bank addressing and refresh.
// Electrical setup/hold margins require implementation timing and board tests.
module EG_PHY_SDRAM_2M_32(input clk,ras_n,cas_n,we_n,
    input [10:0] addr,input [1:0] ba,inout [31:0] dq,
    input cs_n,dm0,dm1,dm2,dm3,cke);
    reg [31:0] mem [0:2097151];
    reg [10:0] row [0:3];
    reg [3:0] open_bank=0;
    integer tick=0,refreshes=0,last_refresh=0,activate_tick[0:3];
    reg mode_set=0,drive=0;
    integer latency=0,write_remaining=0,read_remaining=0;
    reg [20:0] read_base,write_base;
    reg [1:0] read_bank,write_bank;
    reg [31:0] out_data;
    wire [3:0] mask={dm3,dm2,dm1,dm0};
    wire [20:0] location={ba,row[ba],addr[7:0]};
    assign dq=drive?out_data:32'bz;
    integer i;
    task masked_write(input [20:0] location);
        begin
            for(i=0;i<4;i=i+1) if(!mask[i]) mem[location][i*8+:8]=dq[i*8+:8];
        end
    endtask
    always @(posedge clk) begin
        tick=tick+1;drive<=0;
        if(write_remaining>0) begin
            masked_write(write_base+4-write_remaining);
            if(write_remaining==1) open_bank[write_bank]=0;
            write_remaining=write_remaining-1;
        end
        if(read_remaining>0) begin
            drive<=1;out_data<=mem[read_base+4-read_remaining];
            if(read_remaining==1) open_bank[read_bank]=0;
            read_remaining=read_remaining-1;
        end
        if(latency>0) begin
            if(latency==1) begin
                drive<=1;out_data<=mem[read_base];read_remaining=3;
            end
            latency=latency-1;
        end
        if(cke && !cs_n) case({ras_n,cas_n,we_n})
            3'b010: open_bank=0;
            3'b001: begin
                if(open_bank || latency || read_remaining || write_remaining)
                    $fatal(1,"Refresh during an active burst");
                refreshes=refreshes+1;last_refresh=tick;
            end
            3'b000: begin
                if(refreshes<8 || addr!=11'h032) $fatal(1,"Bad BL4 initialization");
                mode_set=1;
            end
            3'b011: begin
                if(!mode_set || open_bank[ba]) $fatal(1,"Invalid ACTIVATE");
                row[ba]=addr;open_bank[ba]=1;activate_tick[ba]=tick;
            end
            3'b100,3'b101: begin
                if(!open_bank[ba] || tick-activate_tick[ba]<3 || !addr[10] || addr[1:0]!=0)
                    $fatal(1,"Invalid BL4 access");
                if(!we_n) begin
                    masked_write(location);write_base=location;write_bank=ba;write_remaining=3;
                end else begin read_base=location;read_bank=ba;latency=3;end
            end
        endcase
        if(mode_set && tick-last_refresh>440) $fatal(1,"Refresh overdue");
    end
endmodule

module tb_sdram_native;
    reg pixel_clk=0,soc_clk=0,reset=1;
    always #6.73077 pixel_clk=~pixel_clk;
    always #15.625 soc_clk=~soc_clk;
    reg select=0,write=0;
    reg [1:0] transfer=0;
    reg [31:0] address=0,data=0;
    wire [31:0] response;
    wire we,ready,accept,idle,flush;
    wire [21:0] pixel_address;
    wire [8:0] pixel_data;
    wire [3:0] hdmi;
    wire [7:0] movie_address;
    wire movie_write;
    wire [31:0] movie_data,movie_result;
    AHBlite_Video writer(soc_clk,!reset,select,write,1'b1,address,data,transfer,3'd2,
        response,ready,accept,idle,we,pixel_address,pixel_data,flush,movie_address,movie_write,movie_data,movie_result);
    hdmi_game dut(soc_clk,pixel_clk,1'b0,reset,we,pixel_address,pixel_data,flush,ready,accept,idle,hdmi,movie_address,movie_write,movie_data,movie_result);
    defparam dut.memory.POWERUP_CYCLES=16;
    integer i,j,k,location,lane,checked=0;
    integer queue_stalls=0,cache_stalls=0,reads=0,writes=0;
    reg checking=0;
    reg [7:0] expected;
    realtime start_time,end_time;
    function [15:0] rgb565(input [7:0] c);
        rgb565={c[7:5],2'b0,c[4:2],3'b0,c[1:0],3'b0};
    endfunction
    function [7:0] colour(input integer x,input integer y);
        colour=(y+53*(x/120))&255;
    endfunction
    task bus_write(input [7:0] a,input [31:0] value);
        begin
            @(negedge soc_clk);address=a;select=1;write=1;transfer=2;
            @(negedge soc_clk);select=0;transfer=0;data=value;
            @(negedge soc_clk);
        end
    endtask
    task room;
        begin
            while(writer.pressure) begin queue_stalls=queue_stalls+1;@(negedge soc_clk);end
        end
    endtask
    task fence;
        begin
            room();bus_write('h90,1);
            wait(writer.flushing);wait(writer.drained);
            if(writer.error || writer.overflow) $fatal(1,"Writer error");
        end
    endtask
    task window(input integer x0,y0,x1,y1);
        begin
            room();bus_write(0,(x1<<16)|x0);bus_write(4,(y1<<16)|y0);bus_write(8,1);
        end
    endtask
    task span(input integer count,input [7:0] colour);
        begin room();bus_write('h70,({16'd0,rgb565(colour)}<<16)|count);end
    endtask
    always @(posedge soc_clk) if(we && !accept) cache_stalls=cache_stalls+1;
    always @(posedge pixel_clk) if(dut.state==dut.ISSUE && dut.mem_ready) begin
        if(dut.operation==0) reads=reads+1;
        else if(dut.operation==1) writes=writes+1;
    end
    initial begin
        #100;reset=0;wait(ready);
        window(0,0,359,639);start_time=$realtime;
        for(j=0;j<640;j=j+1) for(k=0;k<3;k=k+1) span(120,colour(k*120,j));
        fence();end_time=$realtime;
        for(j=0;j<640;j=j+1) for(i=0;i<360;i=i+1) begin
            location=i*160+(639-j)/4;lane=(639-j)%4;
            if(dut.memory.memory.mem[location][lane*8+:8]!==colour(i,j))
                $fatal(1,"Transpose mismatch native x=%0d y=%0d got=%h",i,j,dut.memory.memory.mem[location][lane*8+:8]);
        end
        $display("PASS full frame: 230400 pixels, queue stalls=%0d cache stalls=%0d BL4 writes=%0d reads=%0d render_ms=%0.3f",queue_stalls,cache_stalls,writes,reads,(end_time-start_time)/1e6);
        // Arbitrary partial windows, a band boundary, frame corners, repeated
        // fences and same-word updates must preserve every untouched pixel.
        window(0,0,0,0);span(1,8'hfe);fence();
        window(359,639,359,639);span(1,8'hfc);fence();
        window(10,15,11,16);span(4,8'hfa);fence();
        for(j=0;j<640;j=j+1) for(i=0;i<360;i=i+1) begin
            expected=colour(i,j);
            if(i==0 && j==0) expected=8'hfe;
            if(i==359 && j==639) expected=8'hfc;
            if(i>=10 && i<=11 && j>=15 && j<=16) expected=8'hfa;
            location=i*160+(639-j)/4;lane=(639-j)%4;
            if(dut.memory.memory.mem[location][lane*8+:8]!==expected)
                $fatal(1,"Masked partial-window mismatch x=%0d y=%0d",i,j);
        end
        // Packed RGB565 pairs cross both a native row and a transpose band.
        window(0,15,359,16);span(359,colour(0,15));
        room();bus_write('ha0,{rgb565(8'hf1),rgb565(8'hf2)});fence();
        if(dut.memory.memory.mem[359*160+(639-15)/4][((639-15)%4)*8+:8]!==8'hf2 ||
           dut.memory.memory.mem[(639-16)/4][((639-16)%4)*8+:8]!==8'hf1)
            $fatal(1,"Packed pair across band boundary failed");
        // Restore the test image before full HDMI scanout verification.
        window(0,15,359,16);
        for(j=15;j<=16;j=j+1)for(k=0;k<3;k=k+1)span(120,colour(k*120,j));
        fence();window(10,15,11,16);span(4,8'hfa);fence();
        $display("PASS partial windows, masks, packed pairs and repeated fences");
        wait(dut.y==1080);wait(dut.y==0);checking=1;
        wait(dut.y==1080);checking=0;
        if(checked!=2073600) $fatal(1,"Wrong HDMI pixel count %0d",checked);
        $display("PASS 2073600 HDMI pixels after BL4 transpose and partial writes");$finish;
    end
    integer native_x,native_y;
    always @(posedge pixel_clk) if(checking && dut.active) begin
        native_x=dut.y/3;native_y=639-dut.x/3;
        expected=colour(native_x,native_y);
        if(native_x==0 && native_y==0) expected=8'hfe;
        if(native_x==359 && native_y==639) expected=8'hfc;
        if(native_x>=10 && native_x<=11 && native_y>=15 && native_y<=16) expected=8'hfa;
        #1;
        if(!dut.image_d || dut.pixel!==expected)
            $fatal(1,"HDMI mismatch native x=%0d y=%0d got=%h expected=%h valid=%b",native_x,native_y,dut.pixel,expected,dut.image_d);
        checked=checked+1;
    end
    initial begin #200000000;$fatal(1,"Timeout");end
endmodule
