`timescale 1ns/1ps
// Drive the real AHB span writer and independently read every framebuffer pixel.
module tb_game_writer;
    reg soc_clk=0,pixel_clk=0,reset_n=0,ready=0,hready=1;
    always #15.625 soc_clk=~soc_clk;
    always #12.5 pixel_clk=~pixel_clk;
    reg hsel=0,hwrite=0;
    reg [1:0] htrans=0;
    reg [2:0] hsize=2;
    reg [31:0] haddr=0,hwdata=0;
    wire [31:0] hrdata;
    wire we;
    wire [16:0] wa;
    wire [7:0] wd,rd;
    reg [16:0] ra=0;
    AHBlite_Video video(.HCLK(soc_clk),.HRESETn(reset_n),.HSEL(hsel),.HWRITE(hwrite),
        .HREADY(hready),.HADDR(haddr),.HWDATA(hwdata),.HTRANS(htrans),.HSIZE(hsize),
        .HRDATA(hrdata),.framebuffer_ready(ready),.write_en(we),.write_addr(wa),.write_data(wd));
    hdmi_framebuffer fb(soc_clk,pixel_clk,we,wa,ra,wd,rd);
    integer writes=0,clocks=0,first_clock,last_clock;
    always @(posedge soc_clk) begin
        clocks=clocks+1;
        if(we) begin
            if(wa>=76800) $fatal(1,"Video address out of range: %d",wa);
            writes=writes+1; last_clock=clocks;
        end
    end
    task bus_write(input [7:0] address,input [31:0] value);
        begin
            @(negedge soc_clk); haddr=address; hsel=1; hwrite=1; htrans=2;
            @(negedge soc_clk); hsel=0; htrans=0; hwdata=value;
            @(negedge soc_clk);
        end
    endtask
    task window(input integer x0,y0,x1,y1);
        begin bus_write(0,(x1<<16)|x0); bus_write(4,(y1<<16)|y0); bus_write(8,1); end
    endtask
    task span(input integer count,input [15:0] color);
        begin
            bus_write(8'h80,color); bus_write(8'h84,count);
            wait(!video.busy); repeat(2) @(negedge soc_clk);
        end
    endtask
    task status(input [2:0] wanted);
        begin
            @(negedge soc_clk); haddr=8'h88; hsel=1; hwrite=0; htrans=2;
            @(negedge soc_clk);
            if(hrdata[2:0]!==wanted) $fatal(1,"Status got %h expected %h",hrdata,wanted);
            hsel=0; htrans=0;
        end
    endtask
    function [15:0] color_at(input integer x,y);
        color_at={y[4:0],x[5:0],(y[4:0]^x[4:0])};
    endfunction
    function [7:0] reduced(input [15:0] color);
        reduced={color[15:13],color[10:8],color[4:3]};
    endfunction
    task check(input integer address,input [7:0] wanted);
        begin
            @(negedge pixel_clk); ra=address;
            @(posedge pixel_clk); #1;
            if(rd!==wanted) $fatal(1,"Framebuffer address %d got %h expected %h",address,rd,wanted);
        end
    endtask
    integer x,y,prior_writes;
    initial begin
        #101 reset_n=1;
        status(0); window(0,0,239,319); span(10,16'hffff);
        if(writes!=0) $fatal(1,"Wrote before framebuffer ready");
        status(2); ready=1; bus_write(8'h88,2);
        window(0,0,239,319);
        bus_write(8'h80,16'hffff); bus_write(8'h84,76800);
        first_clock=clocks;
        wait(!video.busy); repeat(2) @(negedge soc_clk);
        if(writes!=76800 || last_clock-first_clock!=76800) $fatal(1,"Full 17-bit fill or one-pixel-per-clock throughput");
        status(4); check(0,255); check(32767,255); check(32768,255); check(65536,255); check(76799,255);
        prior_writes=writes;
        window(0,0,239,319);
        for(y=0;y<320;y=y+1)
            for(x=0;x<240;x=x+30) span(30,color_at(x,y));
        if(writes-prior_writes!=76800) $fatal(1,"Lost/duplicated span writes");
        for(y=0;y<320;y=y+1)
            for(x=0;x<240;x=x+1) check(y*240+x,reduced(color_at((x/30)*30,y)));
        window(5,260,7,261);
        span(1,16'hf800); span(1,16'h07e0); span(1,16'h001f); span(3,16'hffff);
        check(260*240+5,8'he0); check(260*240+6,8'h1c); check(260*240+7,8'h03);
        check(261*240+5,8'hff); check(261*240+6,8'hff); check(261*240+7,8'hff);
        // Cursor wraps and continues over consecutive commands.
        span(1,16'h001f); span(1,16'hf800);
        check(260*240+5,8'h03); check(260*240+6,8'he0);
        check(260*240+4,reduced(color_at(0,260)));
        prior_writes=writes;
        window(230,310,240,319); span(20,16'hffff);
        window(7,5,3,8); span(1,16'hffff);
        window(0,0,239,320); span(1,16'hffff);
        window(256,0,257,1); span(1,16'hffff);
        window(0,512,1,513); span(1,16'hffff);
        if(writes!=prior_writes) $fatal(1,"Invalid/truncated window wrote pixels");
        status(6); bus_write(8'h88,2); status(4);
        window(0,0,239,319); span(0,16'hffff);
        span(76801,16'hffff);
        if(writes!=prior_writes) $fatal(1,"Invalid count wrote pixels");
        status(6); bus_write(8'h88,2);
        // Busy writes cannot change color, window, or restart the running span.
        window(0,0,239,319); bus_write(8'h80,16'h07e0); bus_write(8'h84,50);
        bus_write(8'h80,16'hf800); bus_write(0,0); bus_write(8'h84,100);
        wait(!video.busy); repeat(2) @(negedge soc_clk);
        if(writes-prior_writes!=50) $fatal(1,"Busy command restarted/corrupted span");
        check(0,8'h1c); check(49,8'h1c); status(6); bus_write(8'h88,2);
        // AHB data-phase stalls must delay the register write without losing it.
        @(negedge soc_clk); hsel=1; hwrite=1; htrans=2; haddr=8'h80;
        @(negedge soc_clk); hsel=0; htrans=0; hready=0; hwdata=16'h001f;
        repeat(4) @(negedge soc_clk);
        if(video.color!==8'h1c) $fatal(1,"Register changed during HREADY stall");
        hready=1; @(negedge soc_clk);
        if(video.color!==8'h03) $fatal(1,"Stalled data phase was lost");
        hsize=1; bus_write(8'h80,16'hffff); hsize=2;
        if(video.color!==8'h03) $fatal(1,"Non-word access changed register");
        // Back-to-back address/data phases: color followed by count.
        window(0,0,239,319); prior_writes=writes;
        @(negedge soc_clk); hsel=1; hwrite=1; htrans=2; haddr=8'h80;
        @(negedge soc_clk); haddr=8'h84; hwdata=16'hf800;
        @(negedge soc_clk); hsel=0; htrans=0; hwdata=7;
        @(negedge soc_clk); wait(!video.busy); repeat(2) @(negedge soc_clk);
        if(writes-prior_writes!=7) $fatal(1,"Pipelined AHB writes lost count");
        check(0,8'he0); check(6,8'he0);
        bus_write(8'h84,1000); repeat(10) @(negedge soc_clk); reset_n=0;
        repeat(2) @(negedge soc_clk); prior_writes=writes; reset_n=1;
        repeat(5) @(negedge soc_clk);
        if(writes!=prior_writes) $fatal(1,"Reset did not stop writer");
        span(1,16'hffff);
        if(writes!=prior_writes) $fatal(1,"Reset retained armed window");
        $display("PASS writer/framebuffer: all 76800 pixels, one pixel/clock, 17-bit fill, HUD windows, bank boundaries, wrap, invalid windows/counts, busy rejection, AHB pipeline/stall/size, reset");
        $finish;
    end
    initial begin #100000000; $fatal(1,"Writer test timeout"); end
endmodule

module tb_game_scaler;
    reg clk=0,reset=1,ready=0;
    always #12.5 clk=~clk;
    wire [11:0] x;
    wire [10:0] y;
    wire active,hs,vs;
    wire [16:0] address;
    wire [7:0] r,g,b;
    wire ad,hd,vd;
    reg [7:0] data;
    hdmi_raster raster(clk,reset,x,y,active,hs,vs);
    hdmi_game_scaler scaler(clk,reset,x,y,active,hs,vs,ready,address,data,r,g,b,ad,hd,vd);
    function [7:0] sample(input integer addr);
        sample=(addr^(addr>>8)^(addr>>16))&255;
    endfunction
    always @(posedge clk) data<=sample(address);
    integer i,px,py,sx,sy,expected_addr,image_count;
    reg in_image;
    reg [7:0] p;
    reg [23:0] expected_rgb;
    initial begin
        repeat(4) @(negedge clk);
        reset=0; image_count=0;
        for(i=0;i<2*2200*1125;i=i+1) begin
            px=i%2200; py=(i/2200)%1125;
            in_image=(px<1920 && py<1080);
            // Physical CCW monitor rotation cancels this clockwise mapping.
            expected_addr=in_image ? ((1919-px)/6)*240+21+py*11/60 : 0;
            if(address!==expected_addr) $fatal(1,"Scaler (%d,%d) addr %d expected %d",px,py,address,expected_addr);
            p=sample(expected_addr);
            expected_rgb=(in_image && ready) ? {p[7:5],p[7:5],p[7:6],p[4:2],p[4:2],p[4:3],{4{p[1:0]}}} : 24'd0;
            @(posedge clk); #1;
            if({r,g,b}!==expected_rgb) $fatal(1,"RGB/latency mismatch at (%d,%d)",px,py);
            if(ad!==(px<1920 && py<1080) || hd!==(px>=2008 && px<2052) || vd!==(py>=1084 && py<1089))
                $fatal(1,"Sync pipeline misalignment");
            if(in_image) image_count=image_count+1;
            @(negedge clk);
            if(i==1000) ready=1;
        end
        if(image_count!=2*1920*1080) $fatal(1,"Scaled image size");
        $display("PASS scaler: two frames, clockwise rotation, 198x320 composition, native 1080p, calibrated 60/11 by 6x mapping, RAM/sync latency, RGB332 expansion, startup blanking");
        $finish;
    end
endmodule
