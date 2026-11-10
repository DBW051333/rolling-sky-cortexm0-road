`timescale 1ns/1ps
// Actual M0 RTL executes the profile firmware and writes through the video AHB
// peripheral. The external ADC is stubbed and the framebuffer is always
// ready, isolating CPU/queue cost from transpose/SDRAM stalls.
module eg_adc(input clk,rstn,output [95:0] levels);
    assign levels=96'd0;
endmodule

module tb_frame_profile;
    reg clk=0,reset_n=0;
    always #15.625 clk=~clk;
    wire video_we,video_flush;
    wire [18:0] video_addr;
    wire [7:0] video_data;
    tri swdio;
    CortexM0_SoC #(.CODE_FILE("../../firmware/code.profile.hex")) soc (
        .clk(clk),.RSTn(reset_n),.SWCLK(1'b0),.SWDIO(swdio),
        .col(4'hf),.row(),.uart_rx(1'b1),.uart_tx(),
        .SD_MISO(1'b1),.SD_CLK(),.SD_CS(),.SD_MOSI(),
        .video_we(video_we),.video_addr(video_addr),.video_data(video_data),
        .video_ready(1'b1),.video_write_ready(1'b1),.video_idle(1'b1),.video_flush(video_flush),.audio_pwm());
    integer cycles=0,start_cycle=0,pixels=0,frames=0;
    reg marker_pending=0;
    always @(posedge clk) if(video_we) begin
        if(video_addr!==(((pixels%360)<<10)|(pixels/360))) $fatal(1,"Incorrect portrait viewport address");
        pixels=pixels+1;
    end
    always @(posedge clk) begin
        cycles=cycles+1;
        if(marker_pending && soc.HREADY) begin
            case(soc.HWDATA)
                17,21,2,6: begin
                    start_cycle=cycles; pixels=0;
                    $display("PROFILE start marker=%0d",soc.HWDATA); $fflush();
                end
                20,24,4,8: begin
                    $display("PROFILE marker=%0d cycles=%0d render_ms=%0.6f pixels=%0d",
                        soc.HWDATA,cycles-start_cycle,(cycles-start_cycle)/32000.0,pixels);
                    $fflush();
                    if(pixels!=230400) $fatal(1,"Incomplete frame");
                    frames=frames+1;
                    if(frames==4) begin $display("PASS profile: two menu and two game frames"); $finish; end
                end
            endcase
        end
        if(soc.HREADY)
            marker_pending<=soc.HTRANS[1] && soc.HWRITE && soc.HADDR==32'h4005008c;
    end
    integer ram_index;
    initial begin
        // Match FPGA BRAM power-up zeros. Startup enters main directly and
        // intentionally does not call a C runtime to clear the ZI region.
        for(ram_index=0;ram_index<4096;ram_index=ram_index+1)
            soc.RAM_DATA.mem[ram_index]=0;
        #200 reset_n=1;
        #2000000000; $fatal(1,"Profile timeout");
    end
endmodule
