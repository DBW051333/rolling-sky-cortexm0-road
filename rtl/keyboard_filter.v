// One event per press. Release must also be stable before a new press is armed.
module keyboard_filter #(parameter DEBOUNCE_TICKS=640000)(
    input clk,rstn,
    input [15:0] key_in,
    output reg [15:0] key_pulse,
    output [15:0] key_held
);
    reg [19:0] count[0:15];
    reg [15:0] stable;
    assign key_held=~stable;
    integer i;
    always @(posedge clk or negedge rstn) begin
        if(!rstn) begin
            stable<=16'hffff; key_pulse<=0;
            for(i=0;i<16;i=i+1) count[i]<=0;
        end else begin
            key_pulse<=0;
            for(i=0;i<16;i=i+1) begin
                if(key_in[i]==stable[i]) count[i]<=0;
                else if(count[i]==DEBOUNCE_TICKS-1) begin
                    count[i]<=0; stable[i]<=key_in[i];
                    if(!key_in[i]) key_pulse[i]<=1;
                end else count[i]<=count[i]+1'b1;
            end
        end
    end
endmodule
