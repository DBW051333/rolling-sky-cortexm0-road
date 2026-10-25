create_clock -name CLK50 -period 20 [get_ports clk_50M]
derive_pll_clocks
