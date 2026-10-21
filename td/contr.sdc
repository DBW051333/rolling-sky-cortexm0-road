create_clock -name CLK50 -period 20 [get_ports clk_50M]
create_clock -name SOC_CLK -period 31.25 [get_nets soc_clk]
