
module CortexM0_SoC #(parameter CODE_FILE = "../firmware/code.hex") (
        input       wire            clk,
        input       wire            RSTn,
        inout       wire            SWDIO,  
        input       wire            SWCLK,
        input wire uart_rx,
        output wire uart_tx,

        input       wire    [3:0]   col,
        output      wire    [3:0]   row,
        //input       wire   [15:0]  key_pulse,

        output wire video_we,
        output wire [21:0] video_addr,
        output wire [8:0] video_data,
        input wire video_ready,
        input wire video_write_ready,
        input wire video_idle,
        output wire video_flush,
        output wire [7:0] movie_address,
        output wire movie_write,
        output wire [31:0] movie_data,
        input wire [31:0] movie_result,
        output wire SD_CLK,SD_CS,SD_MOSI,
        input wire SD_MISO,
        output wire audio_pwm
              
);

//------------------------------------------------------------------------------
// DEBUG IOBUF 
//------------------------------------------------------------------------------

wire SWDO;
wire SWDOEN;
wire SWDI;

assign SWDI = SWDIO;
assign SWDIO = (SWDOEN) ?  SWDO : 1'bz;

//------------------------------------------------------------------------------
// Interrupt
//------------------------------------------------------------------------------

wire [31:0] IRQ;
wire key_interrupt;
wire uart_interrupt;
/*Connect the IRQ with keyboard*/
assign IRQ = {30'd0,uart_interrupt,key_interrupt};
/***************************/

wire RXEV;
assign RXEV = 1'b0;

//------------------------------------------------------------------------------
// AHB
//------------------------------------------------------------------------------

wire [31:0] HADDR;
wire [ 2:0] HBURST;
wire        HMASTLOCK;
wire [ 3:0] HPROT;
wire [ 2:0] HSIZE;
wire [ 1:0] HTRANS;
wire [31:0] HWDATA;
wire        HWRITE;
wire [31:0] HRDATA;
wire        HRESP;
wire        HMASTER;
wire        HREADY;

//------------------------------------------------------------------------------
// RESET AND DEBUG
//------------------------------------------------------------------------------

wire SYSRESETREQ;
reg cpuresetn;

always @(posedge clk or negedge RSTn)begin
        if (~RSTn) cpuresetn <= 1'b0;
        else if (SYSRESETREQ) cpuresetn <= 1'b0;
        else cpuresetn <= 1'b1;
end

wire CDBGPWRUPREQ;
reg CDBGPWRUPACK;

always @(posedge clk or negedge RSTn)begin
        if (~RSTn) CDBGPWRUPACK <= 1'b0;
        else CDBGPWRUPACK <= CDBGPWRUPREQ;
end


//------------------------------------------------------------------------------
// Instantiate Cortex-M0 processor logic level
//------------------------------------------------------------------------------

cortexm0ds_logic u_logic (

        // System inputs
        .FCLK           (clk),           //FREE running clock 
        .SCLK           (clk),           //system clock
        .HCLK           (clk),           //AHB clock
        .DCLK           (clk),           //Debug clock
        .PORESETn       (RSTn),          //Power on reset
        .HRESETn        (cpuresetn),     //AHB and System reset
        .DBGRESETn      (RSTn),          //Debug Reset
        .RSTBYPASS      (1'b0),          //Reset bypass
        .SE             (1'b0),          // dummy scan enable port for synthesis

        // Power management inputs
        .SLEEPHOLDREQn  (1'b1),          // Sleep extension request from PMU
        .WICENREQ       (1'b0),          // WIC enable request from PMU
        .CDBGPWRUPACK   (CDBGPWRUPACK),  // Debug Power Up ACK from PMU

        // Power management outputs
        .CDBGPWRUPREQ   (CDBGPWRUPREQ),
        .SYSRESETREQ    (SYSRESETREQ),

        // System bus
        .HADDR          (HADDR[31:0]),
        .HTRANS         (HTRANS[1:0]),
        .HSIZE          (HSIZE[2:0]),
        .HBURST         (HBURST[2:0]),
        .HPROT          (HPROT[3:0]),
        .HMASTER        (HMASTER),
        .HMASTLOCK      (HMASTLOCK),
        .HWRITE         (HWRITE),
        .HWDATA         (HWDATA[31:0]),
        .HRDATA         (HRDATA[31:0]),
        .HREADY         (HREADY),
        .HRESP          (HRESP),

        // Interrupts
        .IRQ            (IRQ),          //Interrupt
        .NMI            (1'b0),         //Watch dog interrupt
        .IRQLATENCY     (8'h0),
        .ECOREVNUM      (28'h0),

        // Systick
        .STCLKEN        (1'b0),
        .STCALIB        (26'h0),

        // Debug - JTAG or Serial wire
        // Inputs
        .nTRST          (1'b1),
        .SWDITMS        (SWDI),
        .SWCLKTCK       (SWCLK),
        .TDI            (1'b0),
        // Outputs
        .SWDO           (SWDO),
        .SWDOEN         (SWDOEN),

        .DBGRESTART     (1'b0),

        // Event communication
        .RXEV           (RXEV),         // Generate event when a DMA operation completed.
        .EDBGRQ         (1'b0)          // multi-core synchronous halt request
);

//------------------------------------------------------------------------------
// AHBlite Interconncet
//------------------------------------------------------------------------------

wire            HSEL_P0;
wire    [31:0]  HADDR_P0;
wire    [2:0]   HBURST_P0;
wire            HMASTLOCK_P0;
wire    [3:0]   HPROT_P0;
wire    [2:0]   HSIZE_P0;
wire    [1:0]   HTRANS_P0;
wire    [31:0]  HWDATA_P0;
wire            HWRITE_P0;
wire            HREADY_P0;
wire            HREADYOUT_P0;
wire    [31:0]  HRDATA_P0;
wire            HRESP_P0;

wire            HSEL_P1;
wire    [31:0]  HADDR_P1;
wire    [2:0]   HBURST_P1;
wire            HMASTLOCK_P1;
wire    [3:0]   HPROT_P1;
wire    [2:0]   HSIZE_P1;
wire    [1:0]   HTRANS_P1;
wire    [31:0]  HWDATA_P1;
wire            HWRITE_P1;
wire            HREADY_P1;
wire            HREADYOUT_P1;
wire    [31:0]  HRDATA_P1;
wire            HRESP_P1;

wire            HSEL_P2;
wire    [31:0]  HADDR_P2;
wire    [2:0]   HBURST_P2;
wire            HMASTLOCK_P2;
wire    [3:0]   HPROT_P2;
wire    [2:0]   HSIZE_P2;
wire    [1:0]   HTRANS_P2;
wire    [31:0]  HWDATA_P2;
wire            HWRITE_P2;
wire            HREADY_P2;
wire            HREADYOUT_P2;
wire    [31:0]  HRDATA_P2;
wire            HRESP_P2;

wire            HSEL_P3;
wire    [31:0]  HADDR_P3;
wire    [2:0]   HBURST_P3;
wire            HMASTLOCK_P3;
wire    [3:0]   HPROT_P3;
wire    [2:0]   HSIZE_P3;
wire    [1:0]   HTRANS_P3;
wire    [31:0]  HWDATA_P3;
wire            HWRITE_P3;
wire            HREADY_P3;
wire            HREADYOUT_P3;
wire    [31:0]  HRDATA_P3;
wire            HRESP_P3;

AHBlite_Interconnect Interconncet(
        .HCLK           (clk),
        .HRESETn        (cpuresetn),

        // CORE SIDE
        .HADDR          (HADDR),
        .HTRANS         (HTRANS),
        .HSIZE          (HSIZE),
        .HBURST         (HBURST),
        .HPROT          (HPROT),
        .HMASTLOCK      (HMASTLOCK),
        .HWRITE         (HWRITE),
        .HWDATA         (HWDATA),
        .HRDATA         (HRDATA),
        .HREADY         (HREADY),
        .HRESP          (HRESP),

        // P0
        .HSEL_P0        (HSEL_P0),
        .HADDR_P0       (HADDR_P0),
        .HBURST_P0      (HBURST_P0),
        .HMASTLOCK_P0   (HMASTLOCK_P0),
        .HPROT_P0       (HPROT_P0),
        .HSIZE_P0       (HSIZE_P0),
        .HTRANS_P0      (HTRANS_P0),
        .HWDATA_P0      (HWDATA_P0),
        .HWRITE_P0      (HWRITE_P0),
        .HREADY_P0      (HREADY_P0),
        .HREADYOUT_P0   (HREADYOUT_P0),
        .HRDATA_P0      (HRDATA_P0),
        .HRESP_P0       (HRESP_P0),

        // P1
        .HSEL_P1        (HSEL_P1),
        .HADDR_P1       (HADDR_P1),
        .HBURST_P1      (HBURST_P1),
        .HMASTLOCK_P1   (HMASTLOCK_P1),
        .HPROT_P1       (HPROT_P1),
        .HSIZE_P1       (HSIZE_P1),
        .HTRANS_P1      (HTRANS_P1),
        .HWDATA_P1      (HWDATA_P1),
        .HWRITE_P1      (HWRITE_P1),
        .HREADY_P1      (HREADY_P1),
        .HREADYOUT_P1   (HREADYOUT_P1),
        .HRDATA_P1      (HRDATA_P1),
        .HRESP_P1       (HRESP_P1),

        // P2
        .HSEL_P2        (HSEL_P2),
        .HADDR_P2       (HADDR_P2),
        .HBURST_P2      (HBURST_P2),
        .HMASTLOCK_P2   (HMASTLOCK_P2),
        .HPROT_P2       (HPROT_P2),
        .HSIZE_P2       (HSIZE_P2),
        .HTRANS_P2      (HTRANS_P2),
        .HWDATA_P2      (HWDATA_P2),
        .HWRITE_P2      (HWRITE_P2),
        .HREADY_P2      (HREADY_P2),
        .HREADYOUT_P2   (HREADYOUT_P2),
        .HRDATA_P2      (HRDATA_P2),
        .HRESP_P2       (HRESP_P2),

        // P3
        .HSEL_P3        (HSEL_P3),
        .HADDR_P3       (HADDR_P3),
        .HBURST_P3      (HBURST_P3),
        .HMASTLOCK_P3   (HMASTLOCK_P3),
        .HPROT_P3       (HPROT_P3),
        .HSIZE_P3       (HSIZE_P3),
        .HTRANS_P3      (HTRANS_P3),
        .HWDATA_P3      (HWDATA_P3),
        .HWRITE_P3      (HWRITE_P3),
        .HREADY_P3      (HREADY_P3),
        .HREADYOUT_P3   (HREADYOUT_P3),
        .HRDATA_P3      (HRDATA_P3),
        .HRESP_P3       (HRESP_P3)
);

//------------------------------------------------------------------------------
// AHB RAMCODE
//------------------------------------------------------------------------------

wire [31:0] RAMCODE_RDATA,RAMCODE_WDATA;
wire [12:0] RAMCODE_WADDR;
wire [12:0] RAMCODE_RADDR;
wire [3:0]  RAMCODE_WRITE;

AHBlite_Block_RAM #(.ADDR_WIDTH(13)) RAMCODE_Interface(
        /* Connect to Interconnect Port 0 */
        .HCLK           (clk),
        .HRESETn        (cpuresetn),
        .HSEL           (HSEL_P0),
        .HADDR          (HADDR_P0),
        .HPROT          (HPROT_P0),
        .HSIZE          (HSIZE_P0),
        .HTRANS         (HTRANS_P0),
        .HWDATA         (HWDATA_P0),
        .HWRITE         (HWRITE_P0),
        .HRDATA         (HRDATA_P0),
        .HREADY         (HREADY_P0),
        .HREADYOUT      (HREADYOUT_P0),
        .HRESP          (HRESP_P0),
        .BRAM_WRADDR    (RAMCODE_WADDR),
        .BRAM_RDADDR    (RAMCODE_RADDR),
        .BRAM_RDATA     (RAMCODE_RDATA),
        .BRAM_WDATA     (RAMCODE_WDATA),
        .BRAM_WRITE     (RAMCODE_WRITE)
        /**********************************/
);

//------------------------------------------------------------------------------
// AHB RAMDATA
//------------------------------------------------------------------------------

wire [31:0] RAMDATA_RDATA;
wire [31:0] RAMDATA_WDATA;
wire [11:0] RAMDATA_WADDR;
wire [11:0] RAMDATA_RADDR;
wire [3:0]  RAMDATA_WRITE;

AHBlite_Block_RAM #(.ADDR_WIDTH(12)) RAMDATA_Interface(
        /* Connect to Interconnect Port 1 */
        .HCLK           (clk),
        .HRESETn        (cpuresetn),
        .HSEL           (HSEL_P1),
        .HADDR          (HADDR_P1),
        .HPROT          (HPROT_P1),
        .HSIZE          (HSIZE_P1),
        .HTRANS         (HTRANS_P1),
        .HWDATA         (HWDATA_P1),
        .HWRITE         (HWRITE_P1),
        .HRDATA         (HRDATA_P1),
        .HREADY         (HREADY_P1),
        .HREADYOUT      (HREADYOUT_P1),
        .HRESP          (HRESP_P1),
        .BRAM_WRADDR    (RAMDATA_WADDR),
        .BRAM_RDADDR    (RAMDATA_RADDR),
        .BRAM_WDATA     (RAMDATA_WDATA),
        .BRAM_RDATA     (RAMDATA_RDATA),
        .BRAM_WRITE     (RAMDATA_WRITE)
        /**********************************/
);

//------------------------------------------------------------------------------
// AHB Keyboard
//------------------------------------------------------------------------------

wire [15:0] key_data;
wire [15:0] key_held;
wire        key_clear;
wire [95:0] adc_levels;

/* Slide potentiometer on header P2. The converter owns its pins directly, so
 * no top-level port or pin assignment is involved; the firmware reads the
 * channels through the keyboard peripheral's register window. */
eg_adc adc_input(.clk(clk), .rstn(cpuresetn), .levels(adc_levels));

AHBlite_Key_UART Keyboard_Interface(
    .HCLK(clk), .HRESETn(cpuresetn), .HSEL(HSEL_P2), .HADDR(HADDR_P2),
    .HTRANS(HTRANS_P2), .HSIZE(HSIZE_P2), .HPROT(HPROT_P2),
    .HWRITE(HWRITE_P2), .HWDATA(HWDATA_P2), .HREADY(HREADY_P2),
    .HREADYOUT(HREADYOUT_P2), .HRDATA(HRDATA_P2), .HRESP(HRESP_P2),
    .key_data(key_data), .key_held(key_held), .adc_levels(adc_levels),
    .key_clear(key_clear), .uart_irq(uart_interrupt), .uart_rx(uart_rx), .uart_tx(uart_tx)
);

//------------------------------------------------------------------------------
// AHB video / SD / audio
// Port 3 sub-decodes video 0x4005xxxx and SD SPI 0x4006xxxx.
wire [31:0] video_read,sd_read,audio_read;
reg sd_response,audio_response;
wire audio_hsel = HSEL_P3 && HADDR_P3[31:16]==16'h4007;
always @(posedge clk or negedge cpuresetn)
    if(!cpuresetn) sd_response<=0;
    else if(HREADY_P3) sd_response<=HSEL_P3 && HTRANS_P3[1] && HADDR_P3[31:16]==16'h4006;
always @(posedge clk or negedge cpuresetn)
    if(!cpuresetn) audio_response<=0;
    else if(HREADY_P3) audio_response<=audio_hsel && HTRANS_P3[1] && !HWRITE_P3;
assign HRDATA_P3=audio_response?audio_read:sd_response?sd_read:video_read;
assign HREADYOUT_P3=1'b1;
assign HRESP_P3=1'b0;
AHBlite_SD SD_Interface(
    .HCLK(clk),.HRESETn(cpuresetn),
    .HSEL(HSEL_P3 && HADDR_P3[31:16]==16'h4006),
    .HADDR(HADDR_P3),.HTRANS(HTRANS_P3),.HWRITE(HWRITE_P3),
    .HWDATA(HWDATA_P3),.HREADY(HREADY_P3),.HRDATA(sd_read),
    .SD_CLK(SD_CLK),.SD_CS(SD_CS),.SD_MOSI(SD_MOSI),.SD_MISO(SD_MISO));
audio_synth Audio(
    .HCLK(clk),.HRESETn(cpuresetn),.HSEL(audio_hsel),
    .HWRITE(HWRITE_P3),.HREADY(HREADY_P3),.HADDR(HADDR_P3),.HWDATA(HWDATA_P3),
    .HTRANS(HTRANS_P3),.HSIZE(HSIZE_P3),.HREADYOUT(),.HRESP(),
    .HRDATA(audio_read),.audio_pwm(audio_pwm));
AHBlite_Video Video_Interface(
    .HCLK(clk),.HRESETn(cpuresetn),
    .HSEL(HSEL_P3 && HADDR_P3[31:16]==16'h4005),
    .HADDR(HADDR_P3),.HTRANS(HTRANS_P3),.HSIZE(HSIZE_P3),
    .HWRITE(HWRITE_P3),.HWDATA(HWDATA_P3),.HREADY(HREADY_P3),.HRDATA(video_read),
    .movie_address(movie_address),.movie_write(movie_write),.movie_data(movie_data),.movie_result(movie_result),
    .framebuffer_ready(video_ready),.framebuffer_write_ready(video_write_ready),.framebuffer_idle(video_idle),.flush(video_flush),.write_en(video_we),.write_addr(video_addr),.write_data(video_data));

//------------------------------------------------------------------------------
// RAM
//------------------------------------------------------------------------------

Block_RAM #(.ADDR_WIDTH(13), .type(1), .INIT_FILE(CODE_FILE)) RAM_CODE(
        .clka           (clk),
        .addra          (RAMCODE_WADDR),
        .addrb          (RAMCODE_RADDR),
        .dina           (RAMCODE_WDATA),
        .doutb          (RAMCODE_RDATA),
        .wea            (RAMCODE_WRITE)
);

Block_RAM #(.ADDR_WIDTH(12)) RAM_DATA(
        .clka           (clk),
        .addra          (RAMDATA_WADDR),
        .addrb          (RAMDATA_RADDR),
        .dina           (RAMDATA_WDATA),
        .doutb          (RAMDATA_RDATA),
        .wea            (RAMDATA_WRITE)
);

//------------------------------------------------------------------------------
// Keyboard
//------------------------------------------------------------------------------
Keyboard Keyboard(
         .clk(clk)
        ,.rstn(cpuresetn)
        ,.key_clear(key_clear)
        ,.col(col)
        ,.row(row)
        //,.key_pulse(key_pulse)
        ,.key_interrupt(key_interrupt)
        ,.key_data(key_data)
        ,.key_held(key_held)
);

endmodule

