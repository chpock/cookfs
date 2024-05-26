# (c) 2024 Konstantin Kushnir

package require tcltest

namespace eval tcltest {
    namespace export makeBinFile viewBinFile
}

proc tcltest::makeBinFile { data args } {
    if { [catch [list uplevel #0 [list tcltest::makeFile {} {*}$args]] res opts] } {
        return -options $opts $res
    }
    set fp [open $res wb]
    if { [string length $data] } {
        puts -nonewline $fp $data
    }
    close $fp
    return $res
}

proc tcltest::viewBinFile { name args } {
    FillFilesExisted
    if { [llength $args] } {
        set dir [lindex $args 0]
    } else {
        set dir [temporaryDirectory]
    }
    set name [file join $dir $name]
    set fp [open $name rb]
    set data [read $fp]
    close $fp
    return $data
}