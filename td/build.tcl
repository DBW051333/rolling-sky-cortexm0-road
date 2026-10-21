open_project rolling_road.al
import_device eagle_s20.db -package EG4S20BG256
elaborate -top top
read_adc contr.adc
optimize_rtl
read_sdc contr.sdc
optimize_gate -maparea rolling_road_gate.area
legalize_phy_inst
export_db rolling_road_gate.db
place
route
report_area -io_info -file rolling_road.area
export_db rolling_road_pr.db
start_timer
report_timing -mode FINAL -net_info -rpt_autogen true -file rolling_road.timing
export_bid rolling_road.bid
bitgen -bit rolling_road.bit
exit
