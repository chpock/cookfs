source [file join [file dirname [info script]] tcltestex.tcl]

#lappend auto_path [pwd]

set tmpdir [file join [tcltest::workingDirectory] _tmp]
catch {file delete -force $tmpdir}
file mkdir $tmpdir
tcltest::temporaryDirectory $tmpdir
tcltest::testsDirectory [file dirname [info script]]

package require vfs::cookfs

catch {
    package require vfs
}

if {[info tclversion] == "8.5"} {
    package require rechan
}

source [file join [tcltest::testsDirectory] common.tcl]

if {[info exists ::env(MEMDEBUG)]} {
    source [file join [tcltest::testsDirectory] memleakhunter.tcl]
}

set constraints [tcltest::configure -constraints]

if {[file exists [file join [tcltest::testsDirectory] .. cookfswriter cookfswriter.tcl]]} {
    source [file join [tcltest::testsDirectory] .. cookfswriter cookfswriter.tcl]
    lappend constraints runCookfsWriter
}

if {[cookfs::pkgconfig get feature-aside]} {
    lappend constraints cookfsAside
}

if {[cookfs::pkgconfig get feature-metadata]} {
    lappend constraints cookfsMetadata
}

tcltest::testConstraint cookfsCompressionNone 1
tcltest::testConstraint cookfsCompressionZlib 1
tcltest::testConstraint cookfsCompressionBz2  [cookfs::pkgconfig get feature-bzip2]
tcltest::testConstraint cookfsCompressionXz   [cookfs::pkgconfig get feature-xz]

tcltest::testConstraint enabledCVfs     [cookfs::pkgconfig get c-vfs]
tcltest::testConstraint disabledCVfs    [expr { ![cookfs::pkgconfig get c-vfs] }]
tcltest::testConstraint enabledCPages   [cookfs::pkgconfig get c-pages]
tcltest::testConstraint disabledCPages  [expr { ![cookfs::pkgconfig get c-pages] }]
tcltest::testConstraint enabledTclCmds  [cookfs::pkgconfig get tcl-commands]
tcltest::testConstraint disabledTclCmds [expr { ![cookfs::pkgconfig get tcl-commands] }]

tcltest::testConstraint packageTclvfs [expr { ![catch { package present vfs }] }]

tcltest::testConstraint tcl86 [expr { $::tcl_version < 9.0 }]
tcltest::testConstraint tcl90 [expr { $::tcl_version >= 9.0 }]

tcltest::configure -constraints $constraints

puts stdout "Tests running in interp:  [info nameofexecutable]"
puts stdout "Tests running in working dir:  $::tcltest::testsDirectory"
if {[llength $::tcltest::skip] > 0} {
    puts stdout "Skipping tests that match:  $::tcltest::skip"
}
if {[llength $::tcltest::match] > 0} {
    puts stdout "Only running tests that match:  $::tcltest::match"
}

if {[llength $::tcltest::skipFiles] > 0} {
    puts stdout "Skipping test files that match:  $::tcltest::skipFiles"
}
if {[llength $::tcltest::matchFiles] > 0} {
    puts stdout "Only sourcing test files that match:  $::tcltest::matchFiles"
}

set timeCmd {clock format [clock seconds]}
puts stdout "Tests began at [eval $timeCmd]"

# source each of the specified tests
foreach file [lsort [::tcltest::getMatchingFiles]] {
    set tail [file tail $file]
    puts stdout $tail
    if {[catch {source $file} msg]} {
	puts stdout $msg
    }
}

# cleanup
puts stdout "\nTests ended at [eval $timeCmd]"
if { $::tcltest::numTests(Total) } { set r $::tcltest::numTests(Failed) } { set r 1 }
::tcltest::cleanupTests 1

if {[info exists ::env(MEMDEBUG)]} {
    incr r [check_memleaks]
}

exit $r
