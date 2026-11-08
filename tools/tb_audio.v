// Regression check for rtl/audio_synth.v.
//
// Drives the synth with the exact AHB write sequence firmware/sound.c emits
// (CTRL=0, clear voices, CTRL=1, then two notes) and watches the duty register.
// The original bug this guards against: the four mix branches assigned acc but
// never advanced tick, so tick stuck at 1019, duty stayed at its 512 reset
// value, and audio_pwm = enable && (tick < duty) was low forever. Every write
// still landed, enable read back as 1, and the board was simply silent.
//
// Run:  cd D:/rolling-sky
//       /d/Verilog/iverilog/bin/iverilog -o /tmp/tb_audio.vvp tools/tb_audio.v rtl/audio_synth.v
//       /d/Verilog/iverilog/bin/vvp /tmp/tb_audio.vvp
`timescale 1ns/1ps
module tb_audio;
reg HCLK=0,HRESETn=0;
reg HSEL=0,HWRITE=0;
reg [1:0] HTRANS=0;
reg [2:0] HSIZE=3'b010;
reg [31:0] HADDR=0,HWDATA=0;
wire HREADYOUT,HRESP,audio_pwm;
wire [31:0] HRDATA;

audio_synth dut(.HCLK(HCLK),.HRESETn(HRESETn),.HSEL(HSEL),.HWRITE(HWRITE),
    .HREADY(1'b1),.HADDR(HADDR),.HWDATA(HWDATA),.HTRANS(HTRANS),.HSIZE(HSIZE),
    .HREADYOUT(HREADYOUT),.HRESP(HRESP),.HRDATA(HRDATA),.audio_pwm(audio_pwm));

always #15.625 HCLK=~HCLK;          // 32 MHz

task ahb_write(input [7:0] idx,input [31:0] data);
begin
    @(posedge HCLK);
    HSEL<=1; HTRANS<=2'b10; HWRITE<=1; HADDR<=32'h40070000+{24'd0,idx}*4;
    @(posedge HCLK);
    HWDATA<=data; HTRANS<=2'b00;
    @(posedge HCLK);
    HSEL<=0; HWRITE<=0;
    @(posedge HCLK);
    HWDATA<=32'h0;
end
endtask

integer i,cyc,lo,hi;
initial begin
    repeat(4) @(posedge HCLK);
    HRESETn<=1;
    repeat(4) @(posedge HCLK);

    // sound_init(): CTRL=0, clear four voices, CTRL=1.
    ahb_write(8'd0,32'h0);
    ahb_write(8'd1,32'h0); ahb_write(8'd2,32'h0); ahb_write(8'd3,32'h0);
    ahb_write(8'd5,32'h0); ahb_write(8'd6,32'h0); ahb_write(8'd7,32'h0);
    ahb_write(8'd9,32'h0); ahb_write(8'd10,32'h0); ahb_write(8'd11,32'h0);
    ahb_write(8'd13,32'h0); ahb_write(8'd14,32'h0); ahb_write(8'd15,32'h0);
    ahb_write(8'd0,32'h1);

    // voice(0, 69, SQUARE, 9): INC = pitch(69) = 14763<<4 = 236208,
    // CTL = wave 0 | volume 9<<4 | gate 0x100 = 0x190.
    ahb_write(8'd1,32'd236208);
    ahb_write(8'd3,32'h190);
    // voice(1, 45, TRI, 10): INC = pitch(45) = 3691<<4 = 59056, CTL = 0x1a1.
    ahb_write(8'd5,32'd59056);
    ahb_write(8'd7,32'h1a1);

    // One 1024-cycle window is exactly one sample period, so the duty register
    // is constant inside a window; the waveform only shows up across windows.
    // 400 windows is 5.6 cycles of A4, enough to catch the square's toggles.
    lo=32767; hi=0;
    for(i=0;i<400;i=i+1) begin
        for(cyc=0;cyc<1024;cyc=cyc+1) @(posedge HCLK);
        if(dut.duty<lo) lo=dut.duty;
        if(dut.duty>hi) hi=dut.duty;
    end

    $display("duty over 400 samples: min=%0d max=%0d swing=%0d (centre 512)",
             lo,hi,hi-lo);
    if(dut.enable!==1'b1) $display("FAIL: synth never enabled");
    else if(hi-lo<100)    $display("FAIL: duty barely moves, pin is a constant level");
    else if(lo==0||hi==1023) $display("FAIL: mix clips into the rails");
    else                  $display("PASS: waveform present, %0d/1024 duty swing",hi-lo);
    $finish;
end
endmodule
