set out_dir [file join [pwd] artifacts cli]
file mkdir $out_dir
set sv_out [file join $out_dir flow_fail.sv]

if {[catch {write_sv -o $sv_out} msg] == 0} {
    error "expected write_sv to fail without a design"
}

set err [last_error]
if {[dict get $err code] ne "NO_DESIGN"} {
    error "expected NO_DESIGN, got [dict get $err code]"
}
