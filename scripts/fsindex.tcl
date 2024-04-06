# Pure Tcl implementation of index handling
#
# (c) 2010-2014 Wojciech Kocjan

namespace eval cookfs {}
namespace eval cookfs::tcl {}
namespace eval cookfs::fsindex {}

set ::cookfs::fsindexhandleIdx 0
proc cookfs::tcl::fsindex {{data ""}} {
    set name ::cookfs::tcl::fsindexhandle[incr ::cookfs::fsindexhandleIdx]
    upvar #0 $name c
    upvar #0 $name.m cm
    upvar #0 $name.b cb

    # initialize metadata array
    array set cm {}

    # initialize block index array
    array set cb {}

    if {$data != ""} {
         ::cookfs::fsindex::import $name $data
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

proc cookfs::fsindex::incrBlockUsage {name block {count 1}} {
    if { $block < 0 } return
    upvar #0 $name.b cb
    incr cb($block) $count
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
    if {[catch {
         upvarPathDir $name $path d tail
    }]} {
         error "Unable to create entry"
    }
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
         foreach {block - -} [lindex $da($tail) 2] {
             incrBlockUsage $name $block -1
         }
         set da($tail) $data
         set d [array get da]
    }
    foreach {block - -} [lindex $data 2] {
        incrBlockUsage $name $block
    }
}

proc cookfs::fsindex::entrySetmtime {name path mtime} {
    if {[catch {
         upvarPathDir $name $path d tail
    }]} {
         error "Unable to create entry"
    }
    array set da $d
    if {![info exists da($tail)]} {
         error "Entry not found"
    }  else  {
         lset da($tail) 0 $mtime
        set d [array get da]
    }
}

proc cookfs::fsindex::entryUnset {name path} {
    if {[catch {
         upvarPathDir $name $path d tail
    }]} {
         error "Unable to unset item"
    }
    array set da $d
    if {[info exists da($tail)]} {
        # check if an entry is non-empty directory
        if {[llength $da($tail)] == 1} {
            if {[catch {
                upvarPath $name $path dl
            }]} {
                error "Unable to unset item"
            }
            if {[llength $dl] > 0} {
                error "Unable to unset item"
            }
        }
        foreach { block - - } [lindex $da($tail) 2] {
            incrBlockUsage $name $block -1
        }
        unset da($tail)
        set d [array get da]
    }  else  {
         error "Unable to unset item"
    }
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
    importMetadata $name data
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
             foreach {chunk - cs} $bosdata {
                  incr size $cs
                  incrBlockUsage $name $chunk
             }
             lappend c($path) $filename [list $time $size $bosdata]
         }
    }
}

proc cookfs::fsindex::export {name} {
    upvar #0 $name c
    return "CFS2.200[exportPath $name {}][exportMetadata $name]"
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

proc cookfs::fsindex::importMetadata {name varname} {
    upvar #0 $name.m cm
    upvar $varname data
    if {([string length $data] >= 4) && ([binary scan $data I count] > 0)} {
         set data [string range $data 4 end]
         for {set i 0} {$i < $count} {incr i} {
             if {![binary scan $data I size]} {
                  set data {}
                  return
             }
             set bindata [string range $data 4 [expr {$size + 3}]]
             set si [string first \0 $bindata]

             if {$si >= 0} {
                  set key [string range $bindata 0 [expr {$si - 1}]]
                  set value [string range $bindata [expr {$si + 1}] end]
                  set cm($key) $value
             }
             set data [string range $data [expr {$size + 4}] end]
         }
    }
}

proc cookfs::fsindex::exportMetadata {name} {
    upvar #0 $name.m cm

    set nl [array names cm]
    set rc [binary format I [llength $nl]]
    foreach n $nl {
         set v $cm($n)
         set size [expr {[string length $n] + [string length $v] + 1}]
         append rc [binary format I $size] $n \0 $v
    }
    return $rc
}

proc cookfs::fsindex::getMetadata {name paramname valuevariable} {
    upvar #0 $name.m cm
    if {[info exists cm($paramname)]} {
         upvar 1 $valuevariable v
         set v $cm($paramname)
         return 1
    }  else  {
         return 0
    }
}

proc cookfs::fsindex::setMetadata {name paramname paramvalue} {
    upvar #0 $name.m cm
    set cm($paramname) $paramvalue
}

proc cookfs::fsindex::unsetMetadata {name paramname} {
    upvar #0 $name.m cm
    unset -nocomplain cm($paramname)
}

proc cookfs::fsindex::getblockusage {name block} {
    upvar #0 $name.b cb
    return [expr { $block >= 0 && [info exists cb($block)] ? $cb($block) : 0 }]
}

proc cookfs::fsindex::handle {name cmd args} {
    switch -- $cmd {
         export {
             return [export $name]
         }
         list {
             if {([llength $args] == 1)} {
                  return [entriesList $name [lindex $args 0]]
             }
         }
         get {
             if {([llength $args] == 1)} {
                  return [entryGet $name [lindex $args 0]]
             }
         }
         getmtime {
             if {([llength $args] == 1)} {
                  return [entryGetmtime $name [lindex $args 0]]
             }
         }
         set {
             if {([llength $args] == 2) || ([llength $args] == 3)} {
                  # directory: path mtime
                  entrySet $name [lindex $args 0] [lrange $args 1 end]
             }
             return ""
         }
         setmtime {
             if {[llength $args] == 2} {
                  entrySetmtime $name [lindex $args 0] [lindex $args 1]
                return ""
             }
         }
         unset {
             if {[llength $args] == 1} {
                  entryUnset $name [lindex $args 0]
                return ""
             }
         }
         delete {
             cleanup $name
             unset $name
             unset $name.m
             rename $name ""
             return ""
         }
         getmetadata {
             if {([llength $args] == 1) || ([llength $args] == 2)} {
                  # name ?defaultValue?
                  set value [lindex $args 1]
                  if {![getMetadata $name [lindex $args 0] value]} {
                      if {[llength $args] == 1} {
                           error "Parameter not defined"
                      }
                  }
                  return $value
             }
         }
         setmetadata {
             if {[llength $args] == 2} {
                  setMetadata $name [lindex $args 0] [lindex $args 1]
                  return ""
             }
         }
         unsetmetadata {
             if {[llength $args] == 1} {
                  unsetMetadata $name [lindex $args 0]
                  return ""
             }
         }
         getblockusage {
             if {[llength $args] != 1} {
                 return -code error "wrong # args: should be \"$name $cmd block\""
             }
             set block [lindex $args 0]
             if {![string is integer -strict $block]} {
                 return -code error "could not get integer from block arg"
             }
             return [getblockusage $name $block]
         }
    }
    error "TODO: args"
}

package provide vfs::cookfs::tcl::fsindex 1.5.0
