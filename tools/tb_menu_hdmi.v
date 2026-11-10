`timescale 1ns/1ps
module tb_menu_hdmi;
    reg pixel_clk=0,soc_clk=0,reset=1;
    always #6.73077 pixel_clk=~pixel_clk;
    always #15.625 soc_clk=~soc_clk;
    reg select=0,write=0;
    reg [1:0] transfer=0;
    reg [31:0] address=0,data=0;
    wire [31:0] response;
    wire we,ready,accept,idle,flush;
    wire [20:0] pixel_address;
    wire [8:0] pixel_data;
    wire [3:0] hdmi;
    wire [7:0] movie_address;
    wire movie_write;
    wire [31:0] movie_data,movie_result;
    AHBlite_Video writer(soc_clk,!reset,select,write,1'b1,address,data,transfer,3'd2,
        response,ready,accept,idle,we,pixel_address,pixel_data,flush,movie_address,movie_write,movie_data,movie_result);
    hdmi_game dut(soc_clk,pixel_clk,1'b0,reset,we,pixel_address,pixel_data,flush,ready,accept,idle,hdmi,movie_address,movie_write,movie_data,movie_result);
    defparam dut.memory.POWERUP_CYCLES=16;
    defparam dut.CLEAR_QUADS=16;

    `include "tmp/menu_player/fixture.vh"
    reg [7:0] compressed[0:PAYLOAD_BYTES-1];
    reg [7:0] decoded[0:460799];
    integer i,j,offset,location,lane,checked=0;
    reg checking=0;
    reg [7:0] expected;
    task bus_write(input [7:0] a,input [31:0] value);
        begin
            @(negedge soc_clk);address=a;select=1;write=1;transfer=2;
            @(negedge soc_clk);select=0;transfer=0;data=value;
            @(negedge soc_clk);
        end
    endtask
    task fence;
        begin bus_write('h90,1);wait(writer.flushing);wait(writer.drained);end
    endtask
    task patch(input integer target,x0,y0,input [15:0] colour);
        begin
            bus_write('hb0,target);bus_write(0,(x0<<16)|x0);
            bus_write(4,(y0<<16)|y0);bus_write(8,1);
            bus_write('h80,colour);bus_write('h84,1);fence();
        end
    endtask
    task descriptor(input [21:0] address,input integer length,time_ms,input [31:0] crc);
        begin bus_write('hd8,address);bus_write('hdc,(time_ms<<15)|length);bus_write('he0,crc);end
    endtask
    initial begin
        $readmemh("tmp/menu_player/compressed.hex",compressed);
        $readmemh("tmp/menu_player/decoded.hex",decoded);
        // Shorten only startup clearing. All frame/overlay addresses remain
        // real, and the independent model still checks every SDRAM command.
        for(i=0;i<'h80000;i=i+1)dut.memory.memory.mem[i]=0;
        #100;reset=0;wait(ready);
        bus_write('hc0,4);bus_write('hd0,OFFSET0);
        for(i=0;i<PAYLOAD_BYTES;i=i+16) begin
            wait(dut.movie.upload_ready);
            for(j=0;j<16;j=j+4)
                bus_write('hd4,{compressed[i+j+3],compressed[i+j+2],compressed[i+j+1],compressed[i+j]});
        end
        wait(dut.movie.upload_ready);
        for(i=0;i<PAYLOAD_BYTES;i=i+1) begin
            offset=(OFFSET0+i)&'h3fffff;
            if(dut.memory.memory.mem['h80000+(offset>>2)][(offset%4)*8+:8]!==compressed[i])
                $fatal(1,"Compressed ring upload mismatch %0d",i);
        end
        $display("PASS ring MMIO uploads through real SDRAM, including wrap");
        // Exercise alpha 1 with RGB332 0, transparency and both UI buffers.
        patch(3,20,16,16'h0001);patch(3,21,15,16'hf800);
        patch(3,22,15,16'hffff);patch(3,22,15,16'h0000);
        patch(2,23,17,16'h07e0);
        if(dut.memory.memory.mem['h40000+20*320+(639-16)/2][16+:16]!==16'h100)
            $fatal(1,"Opaque black UI data mismatch");
        bus_write('he4,1);
        descriptor(OFFSET0,LENGTH0,TIME0,CRC0);
        descriptor(OFFSET1,LENGTH1,TIME1,CRC1);
        bus_write('hc0,3);
        wait(dut.movie.current_time==TIME0);
        for(j=0;j<640;j=j+1)for(i=0;i<360;i=i+1) begin
            location='h10000+i*160+(639-j)/4;lane=(639-j)%4;
            if(dut.memory.memory.mem[location][lane*8+:8]!==decoded[j*360+i])
                $fatal(1,"First decoded frame mismatch %0d %0d",i,j);
        end
        wait(dut.movie.current_time==TIME1);
        if(dut.movie.error)$fatal(1,"Player error");
        for(j=0;j<640;j=j+1)for(i=0;i<360;i=i+1) begin
            location=i*160+(639-j)/4;lane=(639-j)%4;
            if(dut.memory.memory.mem[location][lane*8+:8]!==decoded[230400+j*360+i])
                $fatal(1,"Second decoded frame mismatch %0d %0d",i,j);
        end
        $display("PASS both decoded native framebuffers and independent UI masks");
        wait(dut.y==1080);wait(dut.y==0);checking=1;
        wait(dut.y==1080);checking=0;
        if(checked!=2073600)$fatal(1,"Wrong HDMI pixel count %0d",checked);
        $display("PASS 2073600 HDMI pixels, RLE background plus alpha UI, exact 3x rotation");
        bus_write('hc0,0);wait(!dut.movie.owner);
        patch(0,23,17,16'h001f);
        if(dut.memory.memory.mem[23*160+(639-17)/4][((639-17)%4)*8+:8]!==8'h03)
            $fatal(1,"Game writer after video stop failed");
        wait(!dut.overlay_latched && !dut.mode_latched);
        $display("PASS gameplay writer restored after movie stop");$finish;
    end
    integer native_x,native_y;
    always @(posedge pixel_clk) if(checking && dut.active) begin
        native_x=dut.y/3;native_y=639-dut.x/3;
        expected=decoded[230400+native_y*360+native_x];
        if(native_x==20 && native_y==16)expected=0;
        if(native_x==21 && native_y==15)expected='he0;
        #1;
        if(!dut.image_d || dut.pixel!==expected)
            $fatal(1,"HDMI mismatch x=%0d y=%0d got=%h expected=%h valid=%b",native_x,native_y,dut.pixel,expected,dut.image_d);
        checked=checked+1;
    end
    initial begin #250000000;$fatal(1,"Timeout");end
endmodule
