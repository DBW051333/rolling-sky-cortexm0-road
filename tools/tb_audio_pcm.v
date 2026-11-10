`timescale 1ns/1ps
module tb_audio_pcm;
reg HCLK=0,HRESETn=0,HSEL=0,HWRITE=0;
reg [1:0] HTRANS=0;
reg [2:0] HSIZE=3'b010;
reg [31:0] HADDR=0,HWDATA=0;
wire HREADYOUT,HRESP,audio_pwm;
wire [31:0] HRDATA;
audio_synth dut(.HCLK(HCLK),.HRESETn(HRESETn),.HSEL(HSEL),.HWRITE(HWRITE),
    .HREADY(1'b1),.HADDR(HADDR),.HWDATA(HWDATA),.HTRANS(HTRANS),.HSIZE(HSIZE),
    .HREADYOUT(HREADYOUT),.HRESP(HRESP),.HRDATA(HRDATA),.audio_pwm(audio_pwm));
always #15.625 HCLK=~HCLK;
task ahb_write(input [7:0] idx,input [31:0] data);
begin
    @(posedge HCLK); HSEL<=1; HTRANS<=2'b10; HWRITE<=1;
    HADDR<=32'h40070000+{24'd0,idx}*4;
    @(posedge HCLK); HWDATA<=data; HTRANS<=2'b00;
    @(posedge HCLK); HSEL<=0; HWRITE<=0;
    @(posedge HCLK); HWDATA<=0;
end
endtask
integer i; reg old_req;
initial begin
    repeat(4) @(posedge HCLK); HRESETn<=1; repeat(4) @(posedge HCLK);
    ahb_write(8'd0,32'h3);
    ahb_write(8'd18,32'h40);
    old_req=HRDATA[10];
    for(i=0;i<4;i=i+1) begin
        repeat(1024) @(posedge HCLK);
        if(i<3 && HRDATA[10]!==old_req) $display("FAIL: PCM request toggled early at period %0d",i);
        if(i==3 && HRDATA[10]===old_req) $display("FAIL: PCM request did not toggle");
    end
    if(dut.duty!==10'd256) $display("FAIL: PCM 0x40 duty=%0d",dut.duty);
    ahb_write(8'd18,32'hc0);
    old_req=HRDATA[10];
    repeat(4096) @(posedge HCLK);
    if(dut.duty!==10'd768) $display("FAIL: PCM 0xc0 duty=%0d",dut.duty);
    if(dut.pcm_mode!==1'b1 || dut.enable!==1'b1) $display("FAIL: PCM mode not enabled");
    else $display("PASS: PCM request 8 kHz divider and unsigned 8-bit duty mapping");
    $finish;
end
endmodule
