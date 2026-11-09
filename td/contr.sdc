create_clock -name CLK50 -period 20 [get_ports clk_50M]
# Includes CPU 32 MHz, HDMI pixel 74.285714 MHz and serializer 371.428571 MHz.
# Pixel/serial paths remain timed as related clocks; no blanket false paths.
derive_pll_clocks
