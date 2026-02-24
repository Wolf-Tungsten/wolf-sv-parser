set sv_path [file join $::WOLVRIX_SCRIPT_DIR simple.sv]
set out_dir [file join [pwd] artifacts cli]
file mkdir $out_dir

set_option log.level info
if {[get_option log.level] ne "info"} {
    error "expected log.level to be info"
}
set_option output.dir $out_dir

read_sv $sv_path --top top
transform stats

set json_out flow_ok.json
set sv_out flow_ok.sv

write_json -o $json_out
write_sv -o $sv_out

if {![file exists [file join $out_dir $json_out]]} {
    error "expected json output to exist"
}
if {![file exists [file join $out_dir $sv_out]]} {
    error "expected sv output to exist"
}

set design [show_design]
if {[dict get $design loaded] != 1} {
    error "expected design to be loaded"
}
set stats [show_stats]
if {[dict get $stats graphs] == 0} {
    error "expected non-zero graph count"
}

set graphs [grh_list_graph]
if {[llength $graphs] == 0} {
    error "expected at least one graph"
}
set target [lindex $graphs 0]
grh_select_graph $target
grh_delete_graph $target
set graphs_after [grh_list_graph]
if {[lsearch -exact $graphs_after $target] >= 0} {
    error "graph delete did not remove target"
}

close_design
