// Four-voice wavetable synth + 10-bit PWM DAC for the baseboard's audio path.
//
// The board feeds FPGA pin F3 into two 510 ohm / 33 nF low-pass stages and a
// PAM8403 power amp (see 微处理器原理综合设计底板/板卡和引脚说明.pdf 3.5), so
// all it needs from us is a pulse train whose average follows the waveform.
// A 10-bit counter at 32 MHz gives a 31.25 kHz carrier: well above the 9.4 kHz
// filter corner and above hearing, with 60 dB of resolution, which is enough
// that a chiptune mix does not hiss.
//
// The CPU only pushes notes; it must not feed this at sample rate. Each voice
// carries a 33-bit phase accumulator and a pitch delta, so a laser sweep slides
// smoothly instead of stepping at the frame rate.
//
// The mix is computed across four cycles (ticks 1019..1022) instead of in one:
// the design already closes timing at 27.7 ns of a 31.25 ns clock, so a
// four-way multiply-add in a single cycle would not hold.
//
// Register map, one word each, HADDR[7:2]:
//   0        CTRL    [0] enable
//   1..16    VOICE   four words per voice: INC (23 bit), DELTA (signed 16),
//                    CTL [1:0] wave, [7:4] volume, [8] gate, unused
//   17       STATUS  [9:0] current duty (read-only)
// Waveforms: 0 square, 1 triangle, 2 saw, 3 noise.
module audio_synth(
    input HCLK,HRESETn,HSEL,HWRITE,HREADY,
    input [31:0] HADDR,HWDATA,
    input [1:0] HTRANS,
    input [2:0] HSIZE,
    output HREADYOUT,HRESP,
    output reg [31:0] HRDATA,
    output audio_pwm
);
    assign HREADYOUT=1'b1;
    assign HRESP=1'b0;

    reg [5:0] address;
    reg write_pending;
    reg enable;

    // 24 bit, not 33: firmware's note table is round(f*2^24/31250)>>4 and is
    // written back shifted by 4, so a cycle has to wrap at 2^24.  With a
    // wider accumulator A4 would land at 0.86 Hz -- silent.
    reg [23:0] phase [0:3];
    reg [23:0] inc [0:3];
    reg signed [15:0] dinc [0:3];
    reg [1:0] wave [0:3];
    reg [3:0] vol [0:3];
    reg gate [0:3];

    reg [9:0] tick;
    reg [9:0] duty;
    reg [15:0] lfsr;
    reg signed [15:0] acc;

    // Voice registers start at word 1, so the +1 has to come off before the
    // index is split: otherwise voice 0 lands on field 1 and every INC write
    // becomes a DELTA write, which silently mutes the whole synth.
    wire [4:0] idx=address;
    wire [3:0] vidx=idx-5'd1;
    wire [1:0] vsel=vidx[3:2];
    wire [1:0] vfield=vidx[1:0];

    assign audio_pwm = enable && (tick < duty);

    function signed [7:0] wsample;
        input [1:0] w;
        input [23:0] ph;
        input [15:0] lsb;
        begin
            case(w)
            2'd0: wsample = ph[23] ? 8'sd78 : -8'sd78;
            2'd1: wsample = ph[23] ? (8'sd127 - {1'b0,ph[23:17]})
                                   : (-8'sd127 + {1'b0,ph[23:17]});
            2'd2: wsample = {1'b0,ph[23:17]} - 8'sd64;
            default: wsample = lsb[0] ? 8'sd64 : -8'sd64;
            endcase
        end
    endfunction

    function signed [15:0] vcontrib;
        input signed [7:0] s;
        input [3:0] v;
        input g;
        begin
            vcontrib = g ? ((s * $signed({1'b0,v})) >>> 4) : 16'sd0;
        end
    endfunction

    wire signed [7:0] w0=wsample(wave[0],phase[0],lfsr);
    wire signed [7:0] w1=wsample(wave[1],phase[1],lfsr);
    wire signed [7:0] w2=wsample(wave[2],phase[2],lfsr);
    wire signed [7:0] w3=wsample(wave[3],phase[3],lfsr);
    wire signed [15:0] c0=vcontrib(w0,vol[0],gate[0]);
    wire signed [15:0] c1=vcontrib(w1,vol[1],gate[1]);
    wire signed [15:0] c2=vcontrib(w2,vol[2],gate[2]);
    wire signed [15:0] c3=vcontrib(w3,vol[3],gate[3]);

    integer k;
    always @(posedge HCLK or negedge HRESETn) begin
        if(!HRESETn) begin
            address<=0; write_pending<=0; enable<=0;
            tick<=0; duty<=512; lfsr<=16'hace1; acc<=0;
            for(k=0;k<4;k=k+1) begin
                phase[k]<=0; inc[k]<=0; dinc[k]<=0;
                wave[k]<=0; vol[k]<=0; gate[k]<=0;
            end
        end else begin
            // 32 MHz / 1024 = 31.25 kHz carrier and sample rate. The mix is
            // split over the last four ticks so each cycle carries one
            // multiply-add, then the duty is latched on the wrap. Every
            // branch has to advance tick itself: an earlier version wrote these
            // as a bare if/else-if chain with the mix assignment first and the
            // increment only in the trailing else, so tick stuck at 1019, duty
            // stayed at its 512 reset value, and audio_pwm (tick < duty) was
            // low for good -- a silent synth that still accepted all writes.
            if(tick==10'd1019) begin acc<=c0;       tick<=tick+1'b1; end
            else if(tick==10'd1020) begin acc<=acc+c1; tick<=tick+1'b1; end
            else if(tick==10'd1021) begin acc<=acc+c2; tick<=tick+1'b1; end
            else if(tick==10'd1022) begin acc<=acc+c3; tick<=tick+1'b1; end
            else if(tick==10'd1023) begin
                tick<=0;
                if(acc>16'sd511) duty<=10'd1023;
                else if(acc<-16'sd512) duty<=10'd0;
                else duty<=acc[9:0]+10'd512;
                for(k=0;k<4;k=k+1) begin
                    if(gate[k]) begin
                        phase[k]<=phase[k]+inc[k];
                        inc[k]<=inc[k]+{{8{dinc[k][15]}},dinc[k]};
                    end
                end
            end else tick<=tick+1'b1;

            // The noise source is clocked every cycle, not at the sample rate:
            // it then holds the full 16 MHz bandwidth, so after the board's
            // 9.4 kHz RC filter it is a clean hiss instead of a 2 s burble.
            lfsr<={lfsr[14:0],lfsr[15]^lfsr[13]^lfsr[12]^lfsr[10]};

            if(HREADY) begin
                write_pending<=HSEL && HTRANS[1] && HWRITE && HSIZE==3'b010;
                if(HSEL && HTRANS[1]) address<=HADDR[7:2];
                if(write_pending) begin
                    if(idx==5'd0) enable<=HWDATA[0];
                    else if(idx>=5'd1 && idx<=5'd16) begin
                        case(vfield)
                        2'd0: inc[vsel]<=HWDATA[23:0];
                        2'd1: dinc[vsel]<=HWDATA[15:0];
                        2'd2: begin
                            wave[vsel]<=HWDATA[1:0];
                            vol[vsel]<=HWDATA[7:4];
                            gate[vsel]<=HWDATA[8];
                        end
                        default: ;
                        endcase
                    end
                end
            end
        end
    end

    always @* begin
        HRDATA=0;
        if(idx==5'd17) HRDATA={22'd0,duty};
        else if(idx==5'd0) HRDATA={31'd0,enable};
    end
endmodule
