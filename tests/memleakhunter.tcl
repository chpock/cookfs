# (c) 2024 Konstantin Kushnir

proc snapshot_vars { interp ns } {
    set result [dict create]
    foreach var [interp eval $interp [list info vars "${ns}::*"]] {
        if { $var in {::errorCode ::errorInfo} } continue
        if { [interp eval $interp [list array exists $var]] } {
            dict set result $var [interp eval $interp [list array get $var]]
        } else {
            if { [catch { interp eval $interp [list set $var] } data] } {
                # This is the case where a variable is defined in the namespace
                # but does not yet exist.
                continue
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
    # See comment in tcltest::EvalTest about this hack with 'string range'.
    set opts [string range " $opts" 1 end]
    set res [string range " $res" 1 end]
    return -options $opts $res
}

proc tcltest::EvalTest { code } {
    catch {
        uplevel 1 $code
    } res opts
    # Do not mark objects created after the test has been executed as
    # related to the test.
    memory tag ""
    # After running the test, it is possible that the test result contains
    # objects that were created in the extension. We want to identify
    # all objects that were created in the extension to judge possible
    # memory leaks. However, these objects in interp's result are expected
    # and should not be considered as memory leaks.
    #
    # To avoid detecting such objects, we convert the interp's result to
    # a new string object created from scratch and not associated with
    # the extension. We will do this with the 'string range' command.
    set opts [string range " $opts" 1 end]
    set res [string range " $res" 1 end]
    return -options $opts $res
}

proc tcltest::CleanupTest { code } {

    variable filesMade

    catch {
        uplevel 1 $code
    } res opts

    # See comment in tcltest::EvalTest about this hack with 'string range'.
    set opts [string range " $opts" 1 end]
    set res [string range " $res" 1 end]

    # Commands enclosed in "catch" in cleanup code for a test can leave
    # references to objects created by the extension. I am not sure where they
    # are stored. Unsetting ::errorInfo and ::errorCode does not work,
    # and these references still remain in the interpreter.
    # It looks like the only stable option to remove all possible
    # references is to throw an error in script enclosed in
    # "catch" command.
    catch { unset not_existing_variable_here }

    # cleanup after tclvfs
    if { [array exists ::vfs::_unmountCmd] } {
        if { [array size ::vfs::_unmountCmd] } {
            puts [outputChannel] "WARNING: ::vfs::_unmountCmd is not empty: \[[array get ::vfs::_unmountCmd]\]"
        }
        unset ::vfs::_unmountCmd
    }

    # tcltest::makeFile stores the names of the created files in
    # the filesMade variable. The filename may have been generated by
    # the extension or may contain linked objects (such as a normalized path
    # or internal file system representation) generated by the extension.
    # To clear all references to objects created by the extension, we convert
    # the filenames in this variable to normal strings.
    if { [info exists filesMade] } {
        set result [list]
        foreach fn $filesMade {
            lappend result [string range " $fn" 1 end]
        }
        set filesMade $result
        # fn variable also contains a file name! release it now.
        unset -nocomplain fn
    }

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
            # There are values from thread specific storage. Cookfs initialize
            # them in runtime (when some test is already started) and they will
            # be released during thread termination. Thus, they will be in
            # the list of memory areas allocated during the test execution and
            # not freed when the test is completed. Here we remove these memory
            # region from the memory report.
            set baseline 146
            if { [string match "*/generic/vfsDriver.c [expr { $baseline + 0 }] *" $line] } continue
            if { [string match "*/generic/vfsDriver.c [expr { $baseline + 1 }] *" $line] } continue
            if { [string match "*/generic/vfsDriver.c [expr { $baseline + 2 }] *" $line] } continue
            if { [string match "*/generic/vfsDriver.c [expr { $baseline + 4 }] *" $line] } continue
            if { [string match "*/generic/vfsDriver.c [expr { $baseline + 6 }] *" $line] } continue
            if { ![info exists fo] } {
                set fo [open "x-${::tcltest::testname}.memdump" w]
            }
            puts $fo $line
        }
    }
    close $fi
    if { [info exists fo] } {
        close $fo
    } elseif { [file exists "x-${::tcltest::testname}.memdump"] } {
        file delete "x-${::tcltest::testname}.memdump"
    }
    file delete $memdump

    return -options $opts $res
}

proc check_memleaks {} {
    set found 0
    foreach fn [glob -nocomplain -type f *.memdump] {
        set found 1
        puts ""
        puts "=== Memory leaks in [file rootname [file tail $fn]] ==="
        puts [string trim [read [set fp [open $fn]]]][close $fp]
        puts "================================="
        puts ""
    }
    return $found
}
