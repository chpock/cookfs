package require tcltest

lappend auto_path [pwd]

set tmpdir [file join [file dirname [info script]] _tmp]
catch {file delete -force $tmpdir}
file mkdir $tmpdir
tcltest::temporaryDirectory $tmpdir
tcltest::testsDirectory [file dirname [info script]]

package require vfs::cookfs

if {[info tclversion] == "8.4"} {
    package require rechan
}

source [file join [tcltest::testsDirectory] common.tcl]

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
::tcltest::cleanupTests 1
return

