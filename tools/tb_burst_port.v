`timescale 1ns/1ps
module tb_burst_port;
    reg clk=0,reset=1,request=0,write=0;
    always #6.73077 clk=~clk;
    reg [20:0] address=0;
    reg [127:0] data=0;
    reg [15:0] mask=0;
    wire ready,done;
    wire [127:0] result;
    sdram_word_port #(.POWERUP_CYCLES(16)) dut(clk,reset,request,write,address,data,mask,ready,done,result);
    reg [20:0] locations [0:5];
    reg [127:0] expected,replacement;
    integer i,b;
    task transact(input wr,input [20:0] a,input [127:0] d,input [15:0] m);
        begin
            wait(ready);@(negedge clk);write=wr;address=a;data=d;mask=m;request=1;
            @(negedge clk);request=0;wait(done);@(negedge clk);
        end
    endtask
    initial begin
        locations[0]=0;locations[1]=252;locations[2]=256;
        locations[3]=524288;locations[4]=1048576;locations[5]=2097148;
        #100;reset=0;
        for(i=0;i<6;i=i+1) begin
            expected=128'h0123456789abcdef_fedcba9876543210^i;
            transact(1,locations[i],expected,0);
            transact(0,locations[i],0,0);
            if(result!==expected) $fatal(1,"BL4 round trip at %0d got %h",locations[i],result);
            replacement=128'hffeeccaa_99887766_55443322_1100abcd;
            transact(1,locations[i],replacement,16'h8421);
            for(b=0;b<16;b=b+1) if(!((16'h8421>>b)&1)) expected[b*8+:8]=replacement[b*8+:8];
            transact(0,locations[i],0,0);
            if(result!==expected) $fatal(1,"Per-beat byte mask failure at %0d",locations[i]);
            repeat(500) @(posedge clk);
        end
        $display("PASS BL4: row/bank/end-of-memory boundaries, all beat masks and refresh");$finish;
    end
    initial begin #1000000;$fatal(1,"Timeout");end
endmodule
