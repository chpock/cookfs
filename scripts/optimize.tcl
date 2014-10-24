# cookfs::tcl::optimize --
#
# Optimize file list for minimal reading operations
#
# (c) 2010 Wojciech Kocjan, Pawel Salawa
# (c) 2011-2014 Wojciech Kocjan

namespace eval cookfs {}

proc cookfs::optimizelist {fsid base filelist} {
    upvar #0 $fsid fs

    set largefiles {}
    set smallfiles {}

    foreach file $filelist {
	set path [file join $base $file]
	set chunklist [lindex [$fs(index) get $path] 2]
	if {[llength $chunklist] > 3} {
	    lappend largefiles $file
	}  else  {
	    lappend atpage([lindex $chunklist 0]) $file
	}
    }

    foreach idx [lsort -integer [array names atpage]] {
	set smallfiles [concat $smallfiles $atpage($idx)]
    }

    return [concat $smallfiles $largefiles]
}

package provide vfs::cookfs::tcl::optimize 1.4
