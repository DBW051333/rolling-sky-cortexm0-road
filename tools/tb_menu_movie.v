`timescale 1ns/1ps
module tb_menu_movie;
    reg clk=0,reset=1;
    always #15.625 clk=~clk;
    reg [7:0] address=0;
    reg write=0;
    reg [31:0] data=0;
    wire [31:0] result;
    wire enabled,overlay,owner,valid,back,flush;
    wire [18:0] pixel_address;
    wire [7:0] pixel_data;
    reg ready=0,idle=1;
    wire upload_request,read_request,present_request,present_buffer,ui_request,ui_buffer;
    wire [21:0] upload_address,read_address;
    wire [127:0] upload_data;
    reg upload_ack=0,read_ack=0,present_ack=0,ui_ack=0;
    reg display_front=0;
    reg [127:0] read_data=0;
    menu_movie dut(clk,reset,address,write,data,result,enabled,overlay,owner,
        valid,pixel_address,pixel_data,back,ready,idle,flush,
        upload_request,upload_address,upload_data,upload_ack,
        read_request,read_address,read_ack,read_data,
        present_request,present_buffer,present_ack,ui_request,ui_buffer,ui_ack,display_front);
    `include "tmp/menu_player/fixture.vh"
    reg [7:0] compressed[0:PAYLOAD_BYTES-1];
    reg [7:0] expected[0:460799];
    integer cycle=0,pixels=0,presents=0,frame=0;
    integer read_delay=0,upload_delay=0,present_delay=0,flush_delay=0;
    integer i,base;
    reg monitor=1,corrupt=0;
    task put(input [7:0] a,input [31:0] value);
        begin @(negedge clk);address=a;data=value;write=1;@(negedge clk);write=0;end
    endtask
    task descriptor(input [21:0] offset,input [14:0] length,input [16:0] time_ms,input [31:0] crc);
        begin put('hd8,offset);put('hdc,{time_ms,length});put('he0,crc);end
    endtask
    always @(negedge clk) begin cycle=cycle+1;ready=(cycle%7)<5;end
    always @(posedge clk) if(!reset) begin
        if(valid && ready) begin
            idle<=0;
            if(monitor) begin
                if(pixel_address!==(((pixels%360)<<10)|(pixels/360)))$fatal(1,"Pixel coordinate %0d",pixels);
                if(pixel_data!==expected[frame*230400+pixels])$fatal(1,"Decoded pixel %0d",pixels);
            end
            pixels=pixels+1;
        end
        if(flush)flush_delay<=4;
        else if(flush_delay>0)begin flush_delay<=flush_delay-1;if(flush_delay==1)idle<=1;end
        if(read_request!=read_ack && read_delay==0)read_delay<=3;
        else if(read_delay>0)begin
            read_delay<=read_delay-1;
            if(read_delay==1)begin
                base=(read_address-22'h3ffe00)&22'h3fffff;
                if(base+15>=PAYLOAD_BYTES)$fatal(1,"Read outside prefetched frame");
                for(i=0;i<16;i=i+1)read_data[i*8+:8]<=compressed[base+i];
                if(corrupt && read_address==OFFSET0)read_data[7:0]<=0;
                read_ack<=read_request;
            end
        end
        if(upload_request!=upload_ack && upload_delay==0)upload_delay<=4;
        else if(upload_delay>0)begin upload_delay<=upload_delay-1;if(upload_delay==1)upload_ack<=upload_request;end
        if(present_request!=present_ack && present_delay==0)begin
            if(monitor && pixels!=230400)$fatal(1,"Incomplete presented frame");
            if(present_buffer!==!frame[0])$fatal(1,"Wrong back buffer");
            present_delay<=20;
        end else if(present_delay>0)begin
            present_delay<=present_delay-1;
            if(present_delay==1)begin display_front<=present_buffer;present_ack<=present_request;presents=presents+1;frame=frame+1;pixels=0;end
        end
        if(ui_request!=ui_ack)ui_ack<=ui_request;
    end
    initial begin
        $readmemh("tmp/menu_player/compressed.hex",compressed);
        $readmemh("tmp/menu_player/decoded.hex",expected);
        #100;reset=0;
        put('hd0,22'h3ffff0);
        put('hd4,32'h01020304);put('hd4,32'h11223344);put('hd4,32'h55667788);put('hd4,32'haabbccdd);
        wait(upload_request!=upload_ack);
        if(upload_address!==22'h3ffff0 || upload_data!==128'haabbccdd556677881122334401020304)$fatal(1,"Upload packing");
        wait(upload_ack==upload_request);repeat(4)@(posedge clk);put('hc0,4);
        descriptor(OFFSET0,LENGTH0,TIME0,CRC0);descriptor(OFFSET1,LENGTH1,TIME1,CRC1);
        put('hc0,3);put('he4,1);
        wait(presents==2);repeat(5)@(posedge clk);
        if(dut.error || dut.current_time!=TIME1 || dut.consumed!=((OFFSET1+((LENGTH1+511)&~511))&22'h3fffff))$fatal(1,"Frame metadata or CRC");
        $display("PASS RLE: 460800 source pixels, stalls, ring wrap, CRC, upload and presentation");
        monitor=0;descriptor(OFFSET0,LENGTH0,TIME0,CRC0^1);
        wait(dut.error && dut.state==0);if(presents!=2)$fatal(1,"Bad CRC was presented");
        put('hc0,0);repeat(3)@(posedge clk);put('hc0,4);
        corrupt=1;descriptor(OFFSET0,LENGTH0,TIME0,CRC0);put('hc0,1);
        wait(dut.error && dut.state==0);if(presents!=2)$fatal(1,"Zero run was presented");
        put('hc0,0);repeat(3)@(posedge clk);put('hc0,4);
        corrupt=0;pixels=0;descriptor(OFFSET0,LENGTH0,TIME0,CRC0);put('hc0,1);
        wait(pixels>100);put('hc0,0);wait(!owner);
        if(!idle || presents!=2)$fatal(1,"Stop failed to drain");
        $display("PASS RLE errors and stop: bad CRC, zero run and mid-frame drain");$finish;
    end
    initial begin #200000000;$fatal(1,"Timeout");end
endmodule
