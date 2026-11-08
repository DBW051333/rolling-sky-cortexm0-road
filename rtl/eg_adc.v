// Slide-potentiometer input: the EG4S20 die has one 8-channel 12-bit SAR ADC,
// and the DMZ-EG4S20 board brings channels 1..7 out on the left 2x20 socket
// (P2 pins 33..38 and 32). Channel 0 shares its package ball with the M1 boot
// strap and is not on the header, so it stays disabled.
//
// The converter wants a start pulse at least every 10 us to keep its input
// network settled (TN304 section 5), so the channels are polled back to back
// instead of on demand. One conversion takes a few cycles of the 16 MHz ADC
// clock, so every channel still refreshes faster than the firmware samples.
//
// Results are re-latched into the CPU clock domain: the converter runs on
// clk/2, and the CPU must never read a half-updated sample bank.
module eg_adc(
    input clk,          // 32 MHz SoC clock
    input rstn,
    output reg [95:0] levels   // 8 x 12 bit, indexed by channel number
);
    reg adc_clk;
    always @(posedge clk or negedge rstn)
        if(!rstn) adc_clk<=1'b0;
        else adc_clk<=~adc_clk;      // 16 MHz, the ADC's maximum

    reg soc,pd;
    reg [2:0] channel;
    wire eoc;
    wire [11:0] dout;

    EG_PHY_ADC #(
        .CH0("DISABLE"), .CH1("ENABLE"), .CH2("ENABLE"), .CH3("ENABLE"),
        .CH4("ENABLE"), .CH5("ENABLE"), .CH6("ENABLE"), .CH7("ENABLE"),
        .VREF("ENABLE")             // board drives ADC_VREF (R101 22.1R + C57)
    ) converter (
        .clk(adc_clk), .pd(pd), .s(channel), .soc(soc), .eoc(eoc), .dout(dout)
    );

    localparam IDLE=2'd0, CONVERT=2'd1, NEXT=2'd2;
    reg [1:0] state;
    reg [95:0] sample;

    always @(posedge adc_clk or negedge rstn) begin
        if(!rstn) begin
            state<=IDLE; soc<=1'b0; channel<=3'd1; pd<=1'b0; sample<=96'd0;
        end else case(state)
            IDLE: begin
                soc<=1'b1;            // one-cycle start pulse
                state<=CONVERT;
            end
            CONVERT: begin
                soc<=1'b0;
                if(eoc) begin
                    /* A case rather than a variable part-select: the result of
                     * a conversion always lands in a fixed slice. */
                    case(channel)
                    3'd1: sample[11:0]<=dout;
                    3'd2: sample[23:12]<=dout;
                    3'd3: sample[35:24]<=dout;
                    3'd4: sample[47:36]<=dout;
                    3'd5: sample[59:48]<=dout;
                    3'd6: sample[71:60]<=dout;
                    default: sample[83:72]<=dout;
                    endcase
                    state<=NEXT;
                end
            end
            NEXT: begin               // next channel in the round robin
                channel<=(channel==3'd7)?3'd1:channel+3'd1;
                state<=IDLE;
            end
        endcase
    end

    always @(posedge clk or negedge rstn)
        if(!rstn) levels<=96'd0;
        else levels<=sample;
endmodule
