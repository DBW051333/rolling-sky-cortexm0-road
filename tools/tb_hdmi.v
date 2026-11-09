`timescale 1ns/1ps
// Uses the vendor's functional ODDR model, not a locally invented substitute.
module tb_hdmi_encoder;
    reg clk=0, reset=1, de=0;
    reg [7:0] data=8'hff;
    reg [1:0] control=0;
    wire [9:0] symbol;
    hdmi_tmds_encoder dut(clk,reset,data,de,control,symbol);
    always #12.5 clk=~clk;
    reg [20:0] vectors [0:11999];
    integer count, i;
    initial begin
        if (!$value$plusargs("COUNT=%d", count)) $fatal(1,"Missing COUNT");
        $readmemh("tmp/hdmi_vectors.hex",vectors,0,count-1);
        repeat(3) @(negedge clk);
        reset=0;
        for (i=0;i<=count;i=i+1) begin
            if (i<count) {de,control,data}=vectors[i][20:10];
            @(posedge clk); #1;
            if (i>0 && symbol !== vectors[i-1][9:0])
                $fatal(1,"TMDS vector %0d got %b expected %b",i-1,symbol,vectors[i-1][9:0]);
            @(negedge clk);
        end
        $display("PASS encoder: %0d reference symbols",count);
        $finish;
    end
endmodule

module tb_hdmi_pattern;
    reg clk=0, reset=1;
    always #12.5 clk=~clk;
    wire [11:0] x;
    wire [10:0] y;
    wire active,hs,vs;
    wire [7:0] r,g,b;
    hdmi_test_pattern dut(clk,reset,x,y,active,hs,vs,r,g,b);
    integer frame,i,px,py,visible,hs_count,vs_count;
    initial begin
        repeat(3) @(negedge clk);
        reset=0;
        for(frame=0;frame<2;frame=frame+1) begin
            visible=0; hs_count=0; vs_count=0;
            for(i=0;i<2200*1125;i=i+1) begin
                px=i%2200; py=i/2200;
                if(x!==px || y!==py) $fatal(1,"Bad raster at %d,%d",px,py);
                if(active !== (px<1920 && py<1080)) $fatal(1,"Bad DE");
                if(hs !== (px>=2008 && px<2052)) $fatal(1,"Bad HS");
                if(vs !== (py>=1084 && py<1089)) $fatal(1,"Bad VS");
                if(!active && {r,g,b}!==24'h000000) $fatal(1,"Blank RGB");
                visible=visible+active; hs_count=hs_count+hs; vs_count=vs_count+vs;
                if(py==200 && px%240==120) begin
                    case(px/240)
                        0: if({r,g,b}!==24'hffffff) $fatal(1,"white");
                        1: if({r,g,b}!==24'hffff00) $fatal(1,"yellow");
                        2: if({r,g,b}!==24'h00ffff) $fatal(1,"cyan");
                        3: if({r,g,b}!==24'h00ff00) $fatal(1,"green");
                        4: if({r,g,b}!==24'hff00ff) $fatal(1,"magenta");
                        5: if({r,g,b}!==24'hff0000) $fatal(1,"red");
                        6: if({r,g,b}!==24'h0000ff) $fatal(1,"blue");
                        7: if({r,g,b}!==24'h000000) $fatal(1,"black");
                    endcase
                end
                @(negedge clk);
            end
            if(visible!=2073600 || hs_count!=44*1125 || vs_count!=5*2200)
                $fatal(1,"Bad frame totals");
        end
        $display("PASS raster: 2 frames, 2200x1125 total, 1920x1080 active, positive HS/VS");
        $finish;
    end
endmodule

module tb_hdmi_serializer;
    reg pixel_clk=0, serial_clk=0, reset=1;
    real phase;
    initial begin
        if (!$value$plusargs("PHASE=%f",phase)) phase=0;
        #(phase);
        forever #6.73 pixel_clk=~pixel_clk;
    end
    always #1.346 serial_clk=~serial_clk;
    reg [9:0] number=0;
    wire [39:0] words={10'b1111100000,(number^10'h2aa),~number,number};
    wire [3:0] q;
    hdmi_serializer dut(pixel_clk,serial_clk,reset,words,q);
    always @(posedge pixel_clk or posedge reset)
        if(reset) number<=0;
        else number<=number+1'b1;
    initial begin
        #101 reset=0;
        #18000 reset=1;
        #43 reset=0;
        #18000;
        if(checked<600) $fatal(1,"Insufficient serialized words: %d",checked);
        $display("PASS serializer: phase %0.2f ns, %0d consecutive 40-bit bundles, reset recovery",phase,checked);
        $finish;
    end
    reg previous_clock=0, collecting=0, have_last=0;
    reg [9:0] captured[0:3];
    reg [9:0] last_number;
    integer bit_index=0,lane,checked=0;
    // Recover word boundaries only from the actual forwarded clock pin.
    always @(serial_clk) begin
        #0.3;
        if(reset || $time<400 || ($time>18100 && $time<18500)) begin
            collecting=0; have_last=0;
        end else begin
            if(previous_clock && !q[3]) begin
                if(collecting) begin
                    if(bit_index!=10 || captured[3]!==10'b1111100000)
                        $fatal(1,"Clock word/length %b %d",captured[3],bit_index);
                    if(captured[1]!==~captured[0] || captured[2] !== (captured[0]^10'h2aa))
                        $fatal(1,"Lane alignment/bit order error");
                    if(have_last && captured[0] !== ((last_number+1'b1)&10'h3ff))
                        $fatal(1,"Lost/duplicated pixel: %d -> %d",last_number,captured[0]);
                    last_number=captured[0]; have_last=1; checked=checked+1;
                end
                collecting=1; bit_index=0;
            end
            if(collecting) begin
                if(bit_index>=10) $fatal(1,"Missing clock word boundary");
                for(lane=0;lane<4;lane=lane+1) captured[lane][bit_index]=q[lane];
                bit_index=bit_index+1;
            end
        end
        previous_clock=q[3];
    end
endmodule
