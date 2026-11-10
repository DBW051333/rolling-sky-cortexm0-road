`timescale 1ns/1ps
module tb_native_writer;
    reg clk=0,reset_n=0,select=0,write=0,accept=0,stall=0,cache_idle=1;
    always #5 clk=~clk;
    reg [1:0] transfer=0;
    reg [31:0] address=0,data=0;
    wire [31:0] result;
    wire we,flush;
    wire [20:0] pixel_address;
    wire [8:0] pixel_data;
    AHBlite_Video dut(clk,reset_n,select,write,1'b1,address,data,transfer,
        3'd2,result,1'b1,accept,cache_idle,we,pixel_address,pixel_data,flush,,,,32'd0);
    integer count=0,cycle=0,fence_delay=0;
    reg monitor=1;
    always @(negedge clk) begin cycle=cycle+1;accept=!stall && (cycle%7)<4;end
    always @(posedge clk) begin
        if(we && accept) begin
            cache_idle<=0;
            if(monitor) begin
                if(pixel_address!==(((count%360)<<10)|(count/360)))
                    $fatal(1,"Native coordinate mismatch at %0d",count);
                if(pixel_data[7:0]!==8'he0) $fatal(1,"Colour conversion mismatch");
                count=count+1;
            end
        end
        if(flush) fence_delay<=5;
        else if(fence_delay>0) begin
            fence_delay<=fence_delay-1;
            if(fence_delay==1) cache_idle<=1;
        end
    end
    task bus_write(input [7:0] a,input [31:0] d);
        begin
            @(negedge clk);address=a;select=1;write=1;transfer=2;
            @(negedge clk);select=0;transfer=0;data=d;
            @(negedge clk);
        end
    endtask
    integer i;
    initial begin
        #20;reset_n=1;
        bus_write(0,(359<<16));bus_write(4,(639<<16));bus_write(8,1);
        bus_write('h80,'hf800);bus_write('h84,230400);bus_write('h90,1);
        wait(dut.flushing);wait(dut.drained);
        if(count!=230400 || dut.error || dut.overflow) $fatal(1,"Full frame failed");
        bus_write(0,(360<<16));bus_write(8,1);wait(dut.queued==0);
        if(!dut.error || dut.armed) $fatal(1,"Invalid window accepted");
        bus_write('h88,2);wait(dut.queued==0);
        if(dut.error) $fatal(1,"W1C failed");
        monitor=0;stall=1;
        bus_write(0,(359<<16));bus_write(4,(639<<16));bus_write(8,1);bus_write('h84,10);
        for(i=0;i<70;i=i+1)bus_write('h80,i);
        if(!dut.overflow || !dut.pressure) $fatal(1,"Queue overflow not reported");
        stall=0;wait(dut.queued==0 && !dut.busy);
        bus_write('h88,2);wait(dut.queued==0);
        bus_write('h90,1);wait(dut.flushing);wait(dut.drained);
        if(dut.overflow || dut.error) $fatal(1,"Queue recovery failed");
        $display("PASS native queue: 230400 coordinates, stalls, fence, bounds, overflow and recovery");$finish;
    end
    initial begin #10000000;$fatal(1,"Timeout");end
endmodule
