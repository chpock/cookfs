# (c) 2024 Konstantin Kushnir

proc snapshot_vars { interp ns } {
    set result [dict create]
    foreach var [interp eval $interp [list info vars "${ns}::*"]] {
        if { $var in {::errorCode ::errorInfo} } continue
        if { [interp eval $interp [list array exists $var]] } {
            dict set result $var [interp eval $interp [list array get $var]]
        } else {
            if { [catch { interp eval $interp [list set $var] } data] } {
                set data "@@DONT_EXISTS@@"
            }
            dict set result $var $data
        }
    }
    return $result
}

proc snapshot_procs { interp ns } {
    return [interp eval $interp [list info commands "${ns}::*"]]
}

proc snapshot_chans { interp } {
    return [interp eval $interp [list chan names]]
}

proc snapshot_namespace { interp ns } {
    set result [dict create]
    dict set result vars [snapshot_vars $interp $ns]
    dict set result procs [snapshot_procs $interp $ns]
    return $result
}

proc get_namespaces { interp parent } {
    set result [list $parent]
    foreach ns [interp eval $interp [list namespace children $parent]] {
        lappend result {*}[get_namespaces $interp $ns]
    }
    return $result
}

proc snapshot_interp { interp } {
    set result [dict create]
    dict set result chans [snapshot_chans $interp]
    foreach ns [get_namespaces $interp ""] {
        if { $ns eq "::tcltest" } {
            continue
        }
        dict set result namespaces $ns [snapshot_namespace $interp $ns]
    }
    return $result
}

proc create_snapshot {} {
    set result [dict create]
    foreach interp [list {} {*}[interp slaves]] {
        dict set result $interp [snapshot_interp $interp]
    }
    return $result
}

proc compare_snapshot { old new } {

    set result [list]

    dict for { interp data } $new {
        if { $interp eq "" } {
            set iprefix ""
        } else {
            set iprefix "\[interp $interp\] "
        }
        if { ![dict exists $old $interp] } {
            lappend result "Found new interp: \"$interp\""
            continue
        }
        foreach chan [dict get $data chans] {
            if { [lsearch -exact [dict get $old $interp chans] $chan] == -1 } {
                lappend result "Found new channel: $iprefix\"$chan\""
            }
        }
        dict for { ns data } [dict get $data namespaces] {
            if { ![dict exists $old $interp namespaces $ns] } {
                lappend result "Found new namespace: $iprefix\"$ns\""
                continue
            }
            foreach proc [dict get $data procs] {
                if { [lsearch -exact [dict get $old $interp namespaces $ns procs] $proc] == -1 } {
                    lappend result "Found new proc: $iprefix\"$proc\""
                }
            }
            dict for { var data } [dict get $data vars] {
                if { ![dict exists $old $interp namespaces $ns vars $var] } {
                    lappend result "Found new var: $iprefix\"$var\""
                } elseif { [dict get $old $interp namespaces $ns vars $var] ne $data } {
                    lappend result "Found changed var: $iprefix\"$var\""
                }
            }
        }
    }

    return $result

}

proc current_mem {} {
    lindex [split [lindex [lmap x [split [memory info] "\n"] { if { ![string match {current bytes allocated*} $x] } { continue } { set x } }] 0] " "] end
}

# This is a hack to find out the name of the current test, as we will later
# select memory leaks based on the test name
rename tcltest::test tcltest::test_memdebug
proc tcltest::test { args } {
    set ::tcltest::testname [lindex $args 0]
    tailcall tcltest::test_memdebug {*}$args
}

proc tcltest::SetupTest { code } {
    set ::tcltest::ss [create_snapshot]
    catch {
        uplevel 1 $code
    } res opts
    return -options $opts $res
}

#proc tcltest::EvalTest { code } {
#}

proc tcltest::CleanupTest { code } {

    catch {
        uplevel 1 $code
        memory tag ""
    } res opts

    # cleanup after tclvfs
    catch { unset ::vfs::_unmountCmd }

    foreach msg [compare_snapshot $::tcltest::ss [create_snapshot]] {
        puts [outputChannel] $msg
    }
    unset ::tcltest::ss

    set memdump [file join [temporaryDirectory] memdump]
    catch { file delete -force $memdump }
    memory active $memdump
    set fi [open $memdump r]
    while { [gets $fi line] != -1 } {
        if { [string match "* $::tcltest::testname" $line] } {
            if { [string match "*/generic/tcl*.c * $::tcltest::testname" $line] } continue
            if { [string match "*/unix/tcl*.c * $::tcltest::testname" $line] } continue
            if { [string match "*/win/tcl*.c * $::tcltest::testname" $line] } continue
            if { [string match "*/generic/regcomp.c * $::tcltest::testname" $line] } continue
            if { [string match "*/generic/regc_*.c * $::tcltest::testname" $line] } continue
            if { ![info exists fo] } {
                set fo [open "${::tcltest::testname}.dump" w]
            }
            puts $fo $line
        }
    }
    close $fi
    if { [info exists fo] } {
        close $fo
    }
    file delete -force $memdump

    return -options $opts $res
}