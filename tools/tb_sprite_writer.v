`timescale 1ns/1ps
module tb_sprite_writer;
    reg clk=0,reset_n=0,select=0,write=0,accept=0;
    always #5 clk=~clk;
    reg [1:0] transfer=0;
    reg [31:0] address=0,data=0;
    wire [31:0] result;
    wire we,flush;
    wire [21:0] pixel_address;
    wire [8:0] pixel_data;
    AHBlite_Video dut(clk,reset_n,select,write,1'b1,address,data,transfer,
        3'd2,result,1'b1,accept,1'b1,we,pixel_address,pixel_data,flush,,,,32'd0);
    reg [3:0] skins[0:52223];
    reg [15:0] palettes[0:95];
    integer skin=0,cycle=0,pixels=0,checks=0;
    integer left=122,top=320,x0=113,y0=310,width=133,height=146;
    integer x,y,dx,dy,ink;
    reg red=0,plain=0,monitor=0,tile_test=0,tile_overlay=0,horizontal=0;
    integer scale,camera,edges[0:5],column,edge_start,edge_stop;
    reg [31:0] materials[0:4];
    function integer project(input integer world,input integer factor);
        integer product,delta;
        begin product=world*factor;delta=product<0?-((-product)>>16):product>>16;project=180+((delta*233+128)>>>8);end
    endfunction
    reg [15:0] expected;
    task bus_write(input [7:0] a,input [31:0] d);
        begin
            while(dut.pressure)@(negedge clk);
            @(negedge clk);address=a;select=1;write=1;transfer=2;
            @(negedge clk);select=0;transfer=0;data=d;
            @(negedge clk);
        end
    endtask
    task draw(input integer packed_command);
        begin
            pixels=0;monitor=1;
            bus_write(0,((x0+width-1)<<16)|x0);bus_write(4,((y0+height-1)<<16)|y0);
            bus_write(8,1);bus_write('h74,((top&65535)<<16)|(left&65535));bus_write('h78,red);
            if(packed_command)bus_write(plain?'h70:'h6c,('h001f<<16)|(width*height));
            else begin bus_write('h80,'h001f);bus_write(plain?'h84:'h7c,width*height);end
            bus_write('h90,1);wait(dut.flushing);wait(dut.drained);
            if(pixels!=width*height || dut.error || dut.overflow)$fatal(1,"Incomplete sprite frame");
            monitor=0;checks=checks+1;
        end
    endtask
    always @(negedge clk)begin cycle=cycle+1;accept=(cycle%7)<4;end
    always @(posedge clk)if(we && accept && monitor)begin
        x=x0+pixels%width;y=y0+pixels/width;dx=x-left;dy=y-top;
        expected=16'h001f;
        if(tile_test && x>=edge_start && x<edge_stop)begin
            column=0;while(column<4 && x>=edges[column+1])column=column+1;
            expected=horizontal || x==edges[column]?materials[column][31:16]:materials[column][15:0];
        end
        if((!tile_test && !plain || tile_test && tile_overlay && x>=edge_start && x<edge_stop) && dx>=0 && dx<116 && dy>=0 && dy<128)begin
            ink=skins[skin*4352+((dy*17)>>5)*64+(dx*8+7)/15];
            if(ink!=0)expected=red?16'hf800:palettes[skin*8+ink];
        end
        if(pixel_address!==((x<<10)|y) || pixel_data!=={expected!=0,expected[15:13],expected[10:8],expected[4:3]})
            $fatal(1,"Sprite mismatch skin=%0d x=%0d y=%0d actual=%h expected=%h",skin,x,y,pixel_data,expected);
        pixels=pixels+1;
    end
    integer i,j,a,b,h;
    initial begin
        $readmemh("tmp/game_accel/skins.hex",skins);$readmemh("tmp/game_accel/palettes.hex",palettes);
        #20;reset_n=1;
        for(skin=0;skin<12;skin=skin+1)begin
            bus_write('h9c,0);for(i=0;i<8;i=i+1)bus_write('hac,palettes[skin*8+i]);
            for(i=0;i<68;i=i+1)begin
                bus_write('h94,i*64);for(j=0;j<62;j=j+1)bus_write('h98,skins[skin*4352+i*64+j]);
            end
            draw(skin%2);
        end
        skin=11;red=1;draw(1);
        left=-10;top=-32;x0=0;y0=0;width=110;height=100;draw(1);
        plain=1;draw(1);

        tile_test=1;red=0;x0=0;y0=0;width=360;height=1;
        bus_write('h78,0);bus_write('h44,0);
        for(i=0;i<5;i=i+1)begin materials[i]=((i*16'h0421)<<16)|((i+1)*16'h1248);bus_write('h48,materials[i]);end
        for(a=0;a<19;a=a+1)for(b=1;b<=9;b=b+1)for(h=0;h<2;h=h+1)begin
            scale=12000+a*2100;camera=b*128;horizontal=h;tile_overlay=a%2;
            for(i=0;i<=5;i=i+1)edges[i]=project(i*256-camera,scale);
            edge_start=edges[0]<0?0:edges[0]>360?360:edges[0];
            edge_stop=edges[5]<0?0:edges[5]>360?360:edges[5];
            pixels=0;monitor=1;
            bus_write(0,359<<16);bus_write(4,0);bus_write(8,1);
            if(edge_start)bus_write('h70,(16'h001f<<16)|edge_start);
            bus_write('h60,(scale<<16)|camera);bus_write('h64,horizontal|(tile_overlay<<1));
            if(edge_stop<360)bus_write('h70,(16'h001f<<16)|(360-edge_stop));
            bus_write('h90,1);wait(dut.flushing);wait(dut.drained);
            if(pixels!=360 || dut.error || dut.overflow)$fatal(1,"Tile row incomplete scale=%0d camera=%0d",scale,camera);
            monitor=0;
        end
        $display("PASS hardware road raster: 342 rows, all camera lanes, projection, clipping, grid, material colours and sprite composition");
        $display("PASS hardware sprite: all 12 skins, exact 116x128 scaling, alpha, stalls, red flash, clipping and plain-span recovery (%0d cases)",checks);$finish;
    end
    initial begin #100000000;$fatal(1,"Timeout");end
endmodule
