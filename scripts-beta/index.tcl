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
    array set c $data
}

proc cookfs::fsindex::export {name} {
    upvar #0 $name c
    return [array get c]
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

