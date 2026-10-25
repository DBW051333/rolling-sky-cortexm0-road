// All scanning logic stays in the processor clock domain.
module Keyboard #(parameter SCAN_TICKS=2500,parameter DEBOUNCE_TICKS=640000)(
    input clk,rstn,key_clear,
    input [3:0] col,
    output reg [3:0] row,
    output key_interrupt,
    output reg [15:0] key_data,
    output [15:0] key_held
);
    reg [3:0] col_meta,col_sync;
    reg [15:0] raw;
    reg [1:0] scan_row;
    reg [11:0] count;
    wire [15:0] pulse;
    keyboard_filter #(.DEBOUNCE_TICKS(DEBOUNCE_TICKS)) debounce(.clk(clk),.rstn(rstn),.key_in(raw),.key_pulse(pulse),.key_held(key_held));
    assign key_interrupt=|key_data;
    always @(posedge clk or negedge rstn) begin
        if(!rstn) begin
            row<=4'b1110; raw<=16'hffff; scan_row<=0; count<=0;
            col_meta<=4'hf; col_sync<=4'hf; key_data<=0;
        end else begin
            col_meta<=col; col_sync<=col_meta;
            if(count==SCAN_TICKS-1) begin
                raw[scan_row*4 +: 4]<=col_sync;
                scan_row<=scan_row+1'b1;
                row<={row[2:0],row[3]}; count<=0;
            end else count<=count+1'b1;
            if(key_clear) key_data<=pulse;
            else key_data<=key_data|pulse;
        end
    end
endmodule
