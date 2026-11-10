// EG4S20 package SDRAM, 4 banks x 2048 rows x 256 32-bit words.
// Each request address must be aligned to four 32-bit words.
// Runs at the HDMI pixel clock (74.286 MHz). Commands change on rising
// edges and the SDRAM samples on falling edges. Four-word sequential bursts with auto-precharge
// transactions intentionally leave generous tRCD/tRP/tRFC margins.
module sdram_word_port #(
    parameter POWERUP_CYCLES=16000,
    parameter REFRESH_CYCLES=400
) (
    input clk, reset,
    input request, write,
    input [20:0] address,
    input [127:0] data,
    input [15:0] mask,
    output ready,
    output reg done,
    output reg [127:0] result
);
    localparam START=0, PRECHARGE=1, INIT_REFRESH=2, MODE=3,
               IDLE=4, ACTIVATE=5, ACCESS=6, COMPLETE=7, REFRESH=8;
    reg [3:0] state;
    reg [14:0] timer;
    reg [8:0] refresh_timer;
    reg [3:0] refresh_count;
    reg [20:0] saved_address;
    reg [127:0] saved_data;
    reg [15:0] saved_mask;
    reg saved_write;
    reg [1:0] write_beat;
    reg ras_n,cas_n,we_n,drive;
    reg [10:0] dram_address;
    reg [1:0] bank;
    reg [3:0] byte_mask;
    wire [31:0] dq;
    assign dq=drive ? saved_data[write_beat*32 +: 32] : 32'bz;
    assign ready=state==IDLE && refresh_timer<REFRESH_CYCLES;
    EG_PHY_SDRAM_2M_32 memory (
        .clk(~clk), .cke(1'b1), .cs_n(1'b0),
        .ras_n(ras_n), .cas_n(cas_n), .we_n(we_n),
        .addr(dram_address), .ba(bank), .dq(dq),
        .dm0(byte_mask[0]), .dm1(byte_mask[1]),
        .dm2(byte_mask[2]), .dm3(byte_mask[3])
    );
    always @(posedge clk or posedge reset) begin
        if(reset) begin
            state<=START; timer<=0; refresh_timer<=0; refresh_count<=0;
            ras_n<=1; cas_n<=1; we_n<=1; drive<=0;
            dram_address<=0; bank<=0; byte_mask<=0;
            saved_address<=0; saved_data<=0; saved_mask<=0;
            saved_write<=0; write_beat<=0; done<=0; result<=0;
        end else begin
            ras_n<=1; cas_n<=1; we_n<=1; drive<=0; byte_mask<=0;
            done<=0;
            if(refresh_timer<REFRESH_CYCLES) refresh_timer<=refresh_timer+1'b1;
            case(state)
                START: if(timer==POWERUP_CYCLES-1) begin
                    timer<=0; ras_n<=0; we_n<=0;
                    dram_address<=11'h400; state<=PRECHARGE;
                end else timer<=timer+1'b1;
                PRECHARGE: if(timer==4) begin
                    timer<=0; ras_n<=0; cas_n<=0; state<=INIT_REFRESH;
                end else timer<=timer+1'b1;
                INIT_REFRESH: if(timer==8) begin
                    timer<=0;
                    if(refresh_count==7) begin
                        // Sequential burst length 4, CAS latency 3.
                        ras_n<=0; cas_n<=0; we_n<=0;
                        bank<=0; dram_address<=11'h032; state<=MODE;
                    end else begin
                        refresh_count<=refresh_count+1'b1;
                        ras_n<=0; cas_n<=0;
                    end
                end else timer<=timer+1'b1;
                MODE: if(timer==4) begin
                    timer<=0; refresh_timer<=0; state<=IDLE;
                end else timer<=timer+1'b1;
                IDLE: begin
                    timer<=0;
                    if(refresh_timer>=REFRESH_CYCLES) begin
                        ras_n<=0; cas_n<=0; refresh_timer<=0; state<=REFRESH;
                    end else if(request) begin
                        saved_address<={address[20:2],2'b00}; saved_data<=data;
                        saved_mask<=mask; saved_write<=write;
                        bank<=address[20:19]; dram_address<=address[18:8];
                        ras_n<=0; state<=ACTIVATE;
                    end
                end
                ACTIVATE: if(timer==2) begin
                    timer<=0; cas_n<=0; we_n<=!saved_write;
                    dram_address<={3'b100,saved_address[7:0]};
                    drive<=saved_write; write_beat<=0;
                    byte_mask<=saved_write?saved_mask[3:0]:4'b0;
                    state<=ACCESS;
                end else timer<=timer+1'b1;
                ACCESS: begin
                    if(saved_write && timer<3) begin
                        drive<=1;
                        write_beat<=timer[1:0]+1'b1;
                        case(timer[1:0])
                            0: byte_mask<=saved_mask[7:4];
                            1: byte_mask<=saved_mask[11:8];
                            2: byte_mask<=saved_mask[15:12];
                        endcase
                    end
                    // CL3 data is stable after the third SDRAM clock edge.
                    if(!saved_write && timer>=3 && timer<=6)
                        result[(timer-3)*32 +: 32]<=dq;
                    if(timer==12) begin timer<=0; state<=COMPLETE; end
                    else timer<=timer+1'b1;
                end
                COMPLETE: begin done<=1; state<=IDLE; end
                REFRESH: if(timer==8) begin timer<=0; state<=IDLE; end
                         else timer<=timer+1'b1;
                default: state<=START;
            endcase
        end
    end
endmodule
