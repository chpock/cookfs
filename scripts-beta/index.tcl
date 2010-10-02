# Test pure Tcl implementation of index

namespace eval cookfs {}
namespace eval cookfs::fsindex {}

set ::cookfs::fsindexhandleIdx 0
proc cookfs::fsindex {{data ""}} {
    set name ::cookfs::fsindexhandle[incr ::cookfs::fsindexhandleIdx]
    upvar #0 $name c

    if {$data != ""} {
	fsindex::import $name $data
    }  else  {
	array set c {"" {}}
    }

    interp alias {} $name {} ::cookfs::fsindex::handle $name
    return $name
}

proc cookfs::fsindex::_normalizePath {path} {
    set rc [join [file split $path] /]
    if {$rc == "."} {
	set rc ""
    }
    return $rc
}

proc cookfs::fsindex::upvarPath {name path varName} {
    upvar #0 $name c
    set path [_normalizePath $path]
    if {![info exists c($path)]} {
	error "Not found"
    }
    uplevel 1 [list upvar #0 ${name}($path) $varName]
}

proc cookfs::fsindex::upvarPathDir {name path varName tailVarName} {
    upvar #0 $name c
    uplevel 1 [list set $tailVarName [file tail $path]]
    set dpath [_normalizePath [file dirname $path]]
    if {![info exists c($dpath)]} {
	error "Not found"
    }
    uplevel 1 [list upvar #0 ${name}($dpath) $varName]
}

proc cookfs::fsindex::entriesList {name path} {
    upvarPath $name $path d
    set rc {}
    foreach {n v} $d {
	lappend rc $n
    }
    return [lsort $rc]
}

proc cookfs::fsindex::entryGet {name path} {
    upvar #0 $name c
    upvarPathDir $name $path d tail
    array set da $d
    #puts "GET $path ([info exists da($tail)])"
    if {![info exists da($tail)]} {
	error "Entry not found"
    }
    return $da($tail)
}

proc cookfs::fsindex::entryGetmtime {name path} {
    return [lindex [entryGet $name $path] 0]
}

proc cookfs::fsindex::entrySet {name path data} {
    # TODO: validate entry - data types
    # TODO: validate entry - parent has to be existing directory
    upvar #0 $name c
    upvarPathDir $name $path d tail
    array set da $d
    if {[llength $data] == 2} {
	set size 0
	foreach {- - csize} [lindex $data 1] {
	    incr size $csize
	}
	set data [list [lindex $data 0] $size [lindex $data 1]]
    }

    if {![info exists da($tail)]} {
	lappend d $tail $data
	if {[llength $data] == 1} {
	    # directory
	    set path [_normalizePath $path]
	    set c($path) {}
	}
    }  else  {
	if {[llength $da($tail)] != [llength $data]} {
	    error "Type mismatch"
	}
	set da($tail) $data
	set d [array get da]
    }
}

proc cookfs::fsindex::entrySetmtime {name path mtime} {
}

proc cookfs::fsindex::entryUnset {name path} {
}

proc cookfs::fsindex::cleanup {name} {
}

proc cookfs::fsindex::import {name data} {
    upvar #0 $name c
    if {[string range $data 0 7] != "CFS2.200"} {
	error "Unable to create index object"
    }
    set data [string range $data 8 end]
    importPath $name {} data
}

proc cookfs::fsindex::importPath {name path varname} {
    upvar #0 $name c
    upvar 1 $varname data
    if {[binary scan $data Ia* numitems data] != 2} {
	error "Unable to create index object"
    }
    set c($path) {}
    for {set i 0} {$i < $numitems} {incr i} {
	if {[binary scan $data ca* numchars data] != 2} {
	    error "Unable to create index object"
	}
	set filename [string range $data 0 [expr {$numchars - 1}]]
	set data [string range $data [expr {$numchars + 1}] end]
	set filename [encoding convertfrom utf-8 $filename]
	if {[binary scan $data WIa* time numblocks data] != 3} {
	    error "Unable to create index object"
	}
	set pathname [_normalizePath [file join $path $filename]]
	if {$numblocks == -1} {
	    importPath $name $pathname data
	    lappend c($path) $filename [list $time]
	}  else  {
	    set numdata [expr {$numblocks * 3}]
	    if {[binary scan $data I${numdata}a* bosdata data] != 2} {
		error "Unable to create index object"
	    }
	    set size 0
	    foreach {- - cs} $bosdata {
		incr size $cs
	    }
	    lappend c($path) $filename [list $time $size $bosdata]
	}
    }
}

proc cookfs::fsindex::export {name} {
    upvar #0 $name c
    return "CFS2.200[exportPath $name {}]"
}

proc cookfs::fsindex::exportPath {name path} {
    upvar #0 $name c
    upvarPath $name $path d
    array set da $d

    set rc ""
    append rc [binary format I [llength [array names da]]]
    foreach n [array names da] {
	set un [encoding convertto utf-8 $n]
	append rc [binary format c [string length $un] ] $un \u0000
	append rc [binary format W [lindex $da($n) 0]]
	if {[llength $da($n)] == 1} {
	    append rc [binary format I -1]
	    append rc [exportPath $name [file join $path $n]]
	}  else  {
	    set numblocks [expr {[llength [lindex $da($n) 2]] / 3}]
	    append rc [binary format II* $numblocks [lindex $da($n) 2]]
	}
    }
    return $rc
}

proc cookfs::fsindex::handle {name cmd args} {
    #puts "handle $name $cmd $args"
    switch -- $cmd {
	export {
	    return [export $name]
	}
	list {
	    if {([llength $args] != 1)} {
		error "TODO: args"
	    }
	    return [entriesList $name [lindex $args 0]]
	}
	get {
	    if {([llength $args] != 1)} {
		error "TODO: args"
	    }
	    return [entryGet $name [lindex $args 0]]
	}
	getmtime {
	    if {([llength $args] != 1)} {
		error "TODO: args"
	    }
	    return [entryGetmtime $name [lindex $args 0]]
	}
	set {
	    if {([llength $args] == 2) || ([llength $args] == 3)} {
		# directory: path mtime
		entrySet $name [lindex $args 0] [lrange $args 1 end]
	    }
	}
	setmtime {
	    error "Not implemented"
	}
	unset {
	    error "Not implemented"
	}
	delete {
	    cleanup $name
	    unset $name
	    rename $name ""
	}
	default {
	    error "Not implemented"
	}
    }
}

