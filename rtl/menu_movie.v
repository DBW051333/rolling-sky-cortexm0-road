// Autonomous RGB332 RLE playback from a 4 MiB SDRAM ring.
// CPU supplies sector-aligned compressed frames and descriptors. Pixel work,
// CRC verification and vertical-blank presentation continue during UI drawing.
module menu_movie (
    input clk,reset,
    input [7:0] register_address,
    input register_write,
    input [31:0] register_data,
    output reg [31:0] register_result,
    output reg enabled,overlay_enabled,
    output owner,
    output pixel_valid,
    output [18:0] pixel_address,
    output [7:0] pixel_data,
    output back_buffer,
    input pixel_ready,cache_idle,
    output reg cache_flush,
    output reg upload_request,
    output reg [21:0] upload_address,
    output reg [127:0] upload_data,
    input upload_acknowledge,
    output reg read_request,
    output reg [21:0] read_address,
    input read_acknowledge,
    input [127:0] read_data,
    output reg present_request,present_buffer,
    input present_acknowledge,
    output reg ui_request,ui_buffer,
    input ui_acknowledge,
    input display_front
);
    localparam IDLE=0,LOAD=1,PAIR=2,READ_START=3,READ_WAIT=4,
        DRAW=5,FLUSH=6,FLUSH_WAIT=7,PRESENT_WAIT=8;
    reg [3:0] state;
    reg [95:0] descriptors [0:255];
    reg [95:0] descriptor;
    reg [8:0] producer,consumer;
    reg [21:0] next_offset,source_offset,frame_end,consumed,upload_cursor;
    reg [31:0] next_metadata,expected_crc,crc;
    reg [14:0] bytes_left;
    reg [16:0] frame_time,current_time;
    reg [17:0] decoded;
    reg [8:0] x;
    reg [9:0] y;
    reg [7:0] run_left,run_color;
    reg [127:0] compressed,upload_words;
    reg compressed_valid,back,aborted,error;
    reg [1:0] upload_word,flush_delay;
    reg [1:0] upload_sync,read_sync,present_sync,ui_sync;
    wire [8:0] queued=producer-consumer;
    wire upload_ready=upload_request==upload_sync[1];
    wire [15:0] pair=compressed[source_offset[3:1]*16 +: 16];
    assign owner=enabled || state!=IDLE;
    assign pixel_valid=state==DRAW && enabled && run_left!=0;
    assign pixel_address={x,y};
    assign pixel_data=run_color;
    assign back_buffer=back;
    function [31:0] crc_pair(input [31:0] old,input [15:0] bytes);
        reg [31:0] c;integer bit_number;
        begin
            c=old;
            for(bit_number=0;bit_number<16;bit_number=bit_number+1)
                c=(c[0]^bytes[bit_number]) ? (c>>1)^32'hedb88320 : c>>1;
            crc_pair=c;
        end
    endfunction
    always @(posedge clk) descriptor<=descriptors[consumer[7:0]];
    always @* begin
        register_result=0;
        case(register_address)
            8'hc0: register_result={30'd0,overlay_enabled,enabled};
            8'hc4: register_result={7'd0,queued,10'd0,ui_request!=ui_sync[1],error,
                cache_idle,state!=IDLE,queued<240,upload_ready};
            8'hc8: register_result={10'd0,consumed};
            8'hcc: register_result={15'd0,current_time};
            8'hd0: register_result={10'd0,upload_cursor};
            8'he8: register_result={31'd0,ui_buffer};
        endcase
    end
    always @(posedge clk or posedge reset) begin
        if(reset) begin
            state<=IDLE;enabled<=0;overlay_enabled<=0;producer<=0;consumer<=0;
            next_offset<=0;next_metadata<=0;source_offset<=0;frame_end<=0;consumed<=0;
            upload_cursor<=0;upload_word<=0;upload_words<=0;upload_request<=0;
            upload_address<=0;upload_data<=0;upload_sync<=0;read_sync<=0;
            present_sync<=0;ui_sync<=0;read_request<=0;read_address<=0;
            present_request<=0;present_buffer<=0;ui_request<=0;ui_buffer<=0;
            expected_crc<=0;crc<=0;bytes_left<=0;frame_time<=0;current_time<=0;
            decoded<=0;x<=0;y<=0;run_left<=0;run_color<=0;compressed<=0;
            compressed_valid<=0;back<=1;aborted<=0;error<=0;cache_flush<=0;flush_delay<=0;
        end else begin
            upload_sync<={upload_sync[0],upload_acknowledge};
            read_sync<={read_sync[0],read_acknowledge};
            present_sync<={present_sync[0],present_acknowledge};
            ui_sync<={ui_sync[0],ui_acknowledge};
            cache_flush<=0;
            if(register_write) case(register_address)
                8'hc0: begin
                    enabled<=register_data[0];overlay_enabled<=register_data[1];
                    if(register_data[2] && state==IDLE && cache_idle && upload_ready) begin
                        producer<=0;consumer<=0;consumed<=0;upload_cursor<=0;
                        upload_word<=0;error<=0;back<=1;current_time<=0;
                    end
                end
                8'hd0: if(upload_ready && upload_word==0) upload_cursor<={register_data[21:4],4'd0};
                8'hd4: if(upload_ready) begin
                    upload_words[upload_word*32 +: 32]<=register_data;
                    if(upload_word==3) begin
                        upload_data<={register_data,upload_words[95:0]};
                        upload_address<=upload_cursor;upload_cursor<=upload_cursor+22'd16;
                        upload_request<=~upload_request;upload_word<=0;
                    end else upload_word<=upload_word+1'b1;
                end else error<=1;
                8'hd8: next_offset<=register_data[21:0];
                8'hdc: next_metadata<=register_data;
                8'he0: if(queued<256) begin
                    descriptors[producer[7:0]]<={10'd0,register_data,next_metadata,next_offset};
                    producer<=producer+1'b1;
                end else error<=1;
                8'he4: if(ui_request==ui_sync[1]) begin
                    ui_buffer<=register_data[0];ui_request<=~ui_request;
                end else error<=1;
            endcase
            case(state)
                IDLE: if(enabled && queued!=0) begin back<=~display_front;state<=LOAD;end
                LOAD: begin
                    source_offset<=descriptor[21:0];
                    frame_end<=descriptor[21:0]+((descriptor[36:22]+22'd511)&22'h3ffe00);
                    bytes_left<=descriptor[36:22];frame_time<=descriptor[53:37];
                    expected_crc<=descriptor[85:54];crc<=32'hffffffff;
                    consumer<=consumer+1'b1;decoded<=0;x<=0;y<=0;
                    compressed_valid<=0;aborted<=0;
                    if(descriptor[36:22]==0 || descriptor[22] || descriptor[8:0]!=0) begin
                        error<=1;aborted<=1;state<=FLUSH;
                    end else state<=PAIR;
                end
                PAIR: begin
                    if(!enabled) begin aborted<=1;state<=FLUSH;end
                    else if(bytes_left==0) state<=FLUSH;
                    else if(!compressed_valid) state<=READ_START;
                    else if(pair[7:0]==0 || decoded+{10'd0,pair[7:0]}>230400) begin
                        error<=1;aborted<=1;state<=FLUSH;
                    end else begin
                        run_left<=pair[7:0];run_color<=pair[15:8];
                        bytes_left<=bytes_left-2;crc<=crc_pair(crc,pair);
                        source_offset<=source_offset+22'd2;
                        if(source_offset[3:1]==7) compressed_valid<=0;
                        state<=DRAW;
                    end
                end
                READ_START: if(read_request==read_sync[1]) begin
                    read_address<={source_offset[21:4],4'd0};
                    read_request<=~read_request;state<=READ_WAIT;
                end
                READ_WAIT: if(read_sync[1]==read_request) begin
                    compressed<=read_data;compressed_valid<=1;state<=PAIR;
                end
                DRAW: begin
                    if(!enabled) begin aborted<=1;state<=FLUSH;end
                    else if(pixel_ready) begin
                        decoded<=decoded+1'b1;run_left<=run_left-1'b1;
                        if(x==359) begin x<=0;y<=y+1'b1;end else x<=x+1'b1;
                        if(run_left==1) state<=PAIR;
                    end
                end
                FLUSH: begin cache_flush<=1;flush_delay<=0;state<=FLUSH_WAIT;end
                FLUSH_WAIT: begin
                    if(flush_delay<2) flush_delay<=flush_delay+1'b1;
                    else if(cache_idle) begin
                        if(aborted || !enabled) begin consumed<=frame_end;state<=IDLE;end
                        else if(decoded!=230400 || ~crc!=expected_crc) begin
                            error<=1;consumed<=frame_end;state<=IDLE;
                        end else begin
                            present_buffer<=back;present_request<=~present_request;state<=PRESENT_WAIT;
                        end
                    end
                end
                PRESENT_WAIT: if(present_sync[1]==present_request) begin
                    current_time<=frame_time;back<=~back;consumed<=frame_end;state<=IDLE;
                end
                default: state<=IDLE;
            endcase
        end
    end
endmodule
