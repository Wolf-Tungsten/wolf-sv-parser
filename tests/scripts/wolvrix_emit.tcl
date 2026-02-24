proc getenv {name {default ""}} {
    if {[info exists ::env($name)] && $::env($name) ne ""} {
        return $::env($name)
    }
    return $default
}

set filelist [getenv "WOLVRIX_FILELIST"]
set sources [getenv "WOLVRIX_SOURCES"]

set topName [getenv "WOLVRIX_TOP"]
set svOut [getenv "WOLVRIX_SV_OUT"]
if {$svOut eq ""} {
    error "WOLVRIX_SV_OUT not set"
}

set jsonOut [getenv "WOLVRIX_JSON_OUT"]
if {$jsonOut eq ""} {
    set jsonOut [file join [file dirname $svOut] "grh.json"]
}

set outputDir [getenv "WOLVRIX_OUTPUT_DIR"]
if {$outputDir ne ""} {
    set_option output.dir $outputDir
}

set logLevel [getenv "WOLVRIX_LOG_LEVEL"]
if {$logLevel ne ""} {
    set_option log.level $logLevel
}

set readArgs [list]
if {$filelist ne ""} {
    lappend readArgs -f $filelist
}
if {$sources ne ""} {
    eval lappend readArgs $sources
}
if {$topName ne ""} {
    lappend readArgs --top $topName
}

set extraArgsFile [getenv "WOLVRIX_READ_ARGS_FILE"]
if {$extraArgsFile ne ""} {
    if {![file exists $extraArgsFile]} {
        error "WOLVRIX_READ_ARGS_FILE not found: $extraArgsFile"
    }
    set fh [open $extraArgsFile r]
    set contents [read $fh]
    close $fh
    foreach line [split $contents "\n"] {
        set token [string trim $line]
        if {$token ne ""} {
            lappend readArgs $token
        }
    }
}

set extraArgs [getenv "WOLVRIX_READ_ARGS"]
if {$extraArgs ne ""} {
    eval lappend readArgs $extraArgs
}

if {[llength $readArgs] == 0} {
    error "WOLVRIX_FILELIST or WOLVRIX_SOURCES/WOLVRIX_READ_ARGS must be provided"
}

eval read_sv $readArgs

set skipTransform [getenv "WOLVRIX_SKIP_TRANSFORM" "0"]
if {$skipTransform ne "1"} {
    set passList "xmr-resolve const-fold redundant-elim memory-init-check dead-code-elim stats"
    foreach pass $passList {
        transform $pass
    }
}

set jsonRoundtrip [getenv "WOLVRIX_JSON_ROUNDTRIP" "0"]
set storeJson [getenv "WOLVRIX_STORE_JSON" "0"]

if {$jsonRoundtrip eq "1"} {
    write_json -o $jsonOut
    close_design
    read_json $jsonOut
} elseif {$storeJson eq "1"} {
    write_json -o $jsonOut
}

write_sv -o $svOut
