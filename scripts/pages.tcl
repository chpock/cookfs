# Pure Tcl implementation of pages
#
# (c) 2010 Wojciech Kocjan

package require md5 2

namespace eval cookfs {}
namespace eval cookfs::pages {}

set ::cookfs::pageshandleIdx 0
proc cookfs::pages {args} {
    set name ::cookfs::pageshandle[incr ::cookfs::pageshandleIdx]
    upvar #0 $name c
    array set c {
	readonly 0
	cachelist {}
	compression zlib
	compresscommand ""
	decompresscommand ""
	cachesize 8
	indexdata ""
	endoffset ""
	cfsname "CFS0002"
	lastop read
    }
    while {[llength $args] > 1} {
	switch -- [lindex $args 0] {
	    -endoffset - -compression - -cachesize - -cfsname - -compresscommand - -decompresscommand {
		set c([string range [lindex $args 0] 1 end]) [lindex $args 1 end]
		set args [lrange $args 2 end]
	    }
	    -readonly {
		set c(readonly) 1
		set args [lrange $args 1 end]
	    }
	    -readwrite {
		set c(readonly) 0
		set args [lrange $args 1 end]
	    }

	    default {
		break
	    }
	}
    }

    if {[catch {
	set c(cid) [::cookfs::pages::compression2cid $c(compression)]
    } err]} {
	error $err $err
    }
    if {[llength $args] != 1} {
	error "No filename provided"
    }

    if {[catch {
	if {$c(readonly)} {
	    set c(fh) [open [lindex $args 0] r]
	}  else  {
	    set c(fh) [open [lindex $args 0] a+]
	}
    }]} {
	error "Unable to create Cookfs object"
    }

    if {$c(endoffset) != ""} {
	seek $c(fh) $c(endoffset) start
    }  else  {
	seek $c(fh) 0 end
	set c(endoffset) [tell $c(fh)]
    }

    fconfigure $c(fh) -translation binary

    if {[pages::readIndex $name]} {
	set c(haschanged) 0
    }  else  {
	if {$c(readonly)} {
	    error "Unable to create Cookfs object"
	}
	set c(startoffset) $c(endoffset)
	set c(haschanged) 1
	set c(idx.md5list) {}
	set c(idx.sizelist) {}
    }

    interp alias {} $name {} ::cookfs::pages::handle $name
    return $name
}

proc cookfs::pages::compress {name data} {
    upvar #0 $name c
    if {[string length $data] == 0} {
	return ""
    }
    if {$c(cid) == 1} {
	set data "\u0001[vfs::zip -mode compress -nowrap 1 $data]"
    }  elseif {$c(cid) == 2} {
	package require Trf
	set data "\u0002[binary format I [string length $data]][bz2 -mode compress $data]"
    }  elseif {$c(cid) == 255} {
	if {$c(compresscommand) == ""} {
	    error "No compresscommand specified"
	}
	set data "\u00ff[uplevel #0 [concat $c(compresscommand) [list $data]]]"
    }  else  {
	set data "\u0000$data"
    } 
    return $data
}

proc cookfs::pages::decompress {name data} {
    upvar #0 $name c
    if {[string length $data] == 0} {
	return ""
    }
    if {[binary scan $data ca* cid data] != 2} {
	error "Unable to decompress page"
    }
    switch -- $cid {
	0 {
	    return $data
	}
	1 {
	    return [vfs::zip -mode decompress -nowrap 1 $data]
	}
	2 {
	    package require Trf
	    return [bz2 -mode decompress [string range $data 4 end]]
	}
	255 - -1 {
	    if {$c(decompresscommand) == ""} {
		error "No decompresscommand specified"
	    }
	    return [uplevel #0 [concat $c(decompresscommand) [list $data]]]
	}
    }

    error "Unable to decompress page"
}

proc cookfs::pages::compression2cid {name} {
    switch -- $name {
	none {
	    return 0
	}
	zlib {
	    return 1
	}
	bz2 {
	    return 2
	}
	custom {
	    return 255
	}
	default {
	    error "Unknown compression \"$name\""
	}
    }
}

proc cookfs::pages::cid2compression {name} {
    switch -- $name {
	0 {
	    return "none"
	}
	1 {
	    return "zlib"
	}
	2 {
	    return "bz2"
	}
	255 {
	    return "custom"
	}
	default {
	    error "Unknown compression id \"$name\""
	}
    }
}

proc cookfs::pages::readIndex {name} {
    upvar #0 $name c
    if {[catch {
	seek $c(fh) [expr {$c(endoffset) - 16}] start
	set fc [read $c(fh) 16]
    }]} {
	return 0
    }
    if {[string length $fc] != 16} {
	return 0
    }
    if {[string range $fc 9 15] != "$c(cfsname)"} {
	return 0
    }
    binary scan [string range $fc 0 7] II idxsize numpages

    set idxoffset [expr {$c(endoffset) - (16 + $idxsize + ($numpages * 20))}]
    set c(indexoffset) $idxoffset

    seek $c(fh) $idxoffset start
    set md5data [read $c(fh) [expr {$numpages * 16}]]
    set sizedata [read $c(fh) [expr {$numpages * 4}]]

    set idx [read $c(fh) $idxsize]

    set c(indexdata) [decompress $name $idx]

    set c(idx.md5list) [list]
    set c(idx.sizelist) [list]

    binary scan $md5data H* md5hex
    set md5hex [string toupper $md5hex]
    for {set i 0} {$i < $numpages} {incr i} {
	lappend c(idx.md5list) [string range $md5hex 0 31]
	set md5hex [string range $md5hex 32 end]
    }
    binary scan $sizedata I* c(idx.sizelist)
    foreach s $c(idx.sizelist) {
	incr idxoffset -$s
    }
    set c(startoffset) $idxoffset
    return 1
}

proc cookfs::pages::pageAdd {name contents} {
    upvar #0 $name c
    set md5 [string toupper [md5::md5 -hex $contents]]
    set idx 0
    foreach imd5 $c(idx.md5list) {
	if {[string equal $md5 $imd5]} {
	    if {[string equal [pageGet $name $idx] $contents]} {
		return $idx
	    }
	}
	incr idx
    }
    set contents [compress $name $contents]

    if {!$c(haschanged)} {
	seek $c(fh) $c(indexoffset) start
    }  else  {
	# TODO: optimize not to seek in subsequent writes
	seek $c(fh) 0 end
    }
    if {[catch {
	puts -nonewline $c(fh) $contents
    }]} {
	error "Unable to add page"
    }
    set c(haschanged) 1
    set idx [llength $c(idx.sizelist)]
    lappend c(idx.md5list) $md5
    lappend c(idx.sizelist) [string length $contents]
    return $idx
}

proc cookfs::pages::cleanup {name} {
    upvar #0 $name c
    if {$c(haschanged)} {
	set offset $c(startoffset)
	foreach i $c(idx.sizelist) {
	    incr offset $i
	}
	seek $c(fh) $offset start
	# write MD5 indexes
	puts -nonewline $c(fh) [binary format H* [join $c(idx.md5list) ""]]
	# write size indexes
	puts -nonewline $c(fh) [binary format I* $c(idx.sizelist)]
	set idx [compress $name $c(indexdata)]
	puts -nonewline $c(fh) $idx
	puts -nonewline $c(fh) [binary format IIca* [string length $idx] [llength $c(idx.sizelist)] [compression2cid $c(compression)] $c(cfsname)]
	if {[tell $c(fh)] < $c(endoffset)} {
	    chan truncate $c(fh)
	}
    }
    close $c(fh)
}

proc cookfs::pages::pageGet {name idx} {
    upvar #0 $name c
    set o $c(startoffset)

    set cidx [lsearch -exact $c(cachelist) $idx]
    if {$cidx >= 0} {
	if {$cidx > 0} {
	    set c(cachelist) [linsert [lreplace $c(cachelist) $cidx $cidx] 0 $idx]
	}
	return $c(cache,$idx)
    }

    if {$idx >= [llength $c(idx.sizelist)]} {
	error "Out of range"
    }
    for {set i 0} {$i < $idx} {incr i} {
	incr o [lindex $c(idx.sizelist) $i]
    }
    seek $c(fh) $o start
    set fc [read $c(fh) [lindex $c(idx.sizelist) $idx]]
    set c(lastop) read

    set fc [decompress $name $fc]

    if {$c(cachesize) > 0} {
	if {[llength $c(cachelist)] >= $c(cachesize)} {
	    set remidx [lindex $c(cachelist) [expr {$c(cachesize) - 1}]]
	    unset c(cache,$remidx)
	    set c(cachelist) [lrange $c(cachelist) 0 [expr {$c(cachesize) - 2}]]
	}
	set c(cachelist) [linsert $c(cachelist) 0 $idx]
	set c(cache,$idx) $fc
    }

    return $fc
}

proc cookfs::pages::handle {name cmd args} {
    upvar #0 $name c
    switch -- $cmd {
	get {
	    if {[llength $args] != 1} {
		error "TODO: help"
	    }
	    if {[catch {
		pageGet $name [lindex $args 0]
	    } rc]} {
		# TODO: log error
		error "Unable to retrieve chunk"
	    }
	    return $rc
	}
	add {
	    if {[llength $args] != 1} {
		error "TODO: help"
	    }
	    if {[catch {
		pageAdd $name [lindex $args 0]
	    } rc]} {
		# TODO: log error
		error "Unable to add page"
	    }
	    return $rc
	}
	length {
	    if {[llength $args] != 0} {
		error "TODO: help"
	    }
	    return [llength $c(idx.sizelist)]
	}
	index {
	    if {[llength $args] == 1} {
		set c(indexdata) [lindex $args 0]
	    }  elseif {[llength $args] == 0} {
		return $c(indexdata)
	    }  else  {
		error "TODO: help"
	    }
	}
	delete {
	    cleanup $name
	    unset $name
	    rename $name ""
	}
	dataoffset {
	    error "Not implemented"
	}
	aside - gethead - getheadmd5 - gettail - gettailmd5 {
	    error "Not implemented"
	}
	default {
	    error "Not implemented"
	}
    }
}

package provide vfs::cookfs::tcl::pages 1.1
