# Pure Tcl implementation of pages
#
# (c) 2010 Wojciech Kocjan

package require md5 2

namespace eval cookfs {}
namespace eval cookfs::pages {}

set ::cookfs::pages::errorMessage "Unable to create Cookfs object"

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
        hash md5
        alwayscompress 0
    }

    if {[info commands ::zlib] != ""} {
        set c(_usezlib) 1
    }  else  {
        set c(_usezlib) 0
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
            -alwayscompress {
                set c([string range [lindex $args 0] 1 end]) 1
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
            set c(fh) [open [lindex $args 0] {RDWR CREAT}]
        }
        fconfigure $c(fh) -translation binary
    } err]} {
        catch {close $c(fh)}
        set msg "$::cookfs::pages::errorMessage: $err"
        error $msg $msg
    }

    if {$c(endoffset) != ""} {
        if {[catch {
            seek $c(fh) $c(endoffset) start
        }]} {
            set msg "$::cookfs::pages::errorMessage: index not found"
            error $msg $msg
        }
    }  else  {
        if {[catch {
            seek $c(fh) 0 end
            set fileSize [tell $c(fh)]
            if {$fileSize > 4096} {
                seek $c(fh) -4096 current
            }  else  {
                seek $c(fh) -$fileSize current
            }
            set tail [read $c(fh)]
        }]} {
            set msg "$::cookfs::pages::errorMessage: index not found"
            error $msg $msg
        }
        set tailOffset [string last $c(cfsname) $tail]
        if {$tailOffset >= 0} {
            set c(endoffset) [expr {[tell $c(fh)] - [string length $tail] + $tailOffset + 7}]
            seek $c(fh) $c(endoffset) start
        }  else  {
            set c(endoffset) [tell $c(fh)]
        }
    }

    if {[pages::readIndex $name msg]} {
        set c(haschanged) 0
        set c(indexChanged) 0
    }  else  {
        if {$c(readonly)} {
            catch {close $c(fh)}
            error $msg $msg
        }
        set c(startoffset) $c(endoffset)
        set c(haschanged) 1
        set c(indexChanged) 1
        set c(idx.md5list) {}
        set c(idx.sizelist) {}
    }

    interp alias {} $name {} ::cookfs::pages::handle $name
    return $name
}

proc cookfs::pages::compress {name origdata} {
    upvar #0 $name c
    if {[string length $origdata] == 0} {
        return ""
    }
    if {$c(cid) == 1} {
        if {$c(_usezlib)} {
            set data "\u0001[zlib deflate $origdata]"
        }  else  {
            set data "\u0001[vfs::zip -mode compress -nowrap 1 $origdata]"
        }
    }  elseif {$c(cid) == 2} {
        package require Trf
        set data "\u0002[binary format I [string length $origdata]][bz2 -mode compress $origdata]"
    }  elseif {$c(cid) == 255} {
        if {$c(compresscommand) == ""} {
            error "No compresscommand specified"
        }
        set data "\u00ff[uplevel #0 [concat $c(compresscommand) [list $origdata]]]"
    } 
    # if compression algorithm was not matched or
    #   we should not always compress and compressed data is not smaller, revert to uncompressed data
    if {(![info exists data]) || ((!$c(alwayscompress)) && ([string length $data] > [string length $origdata]))} {
        set data "\u0000$origdata"
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
            if {$c(_usezlib)} {
                return [zlib inflate $data]
            }  else  {
                return [vfs::zip -mode decompress -nowrap 1 $data]
            }
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

proc cookfs::pages::readIndex {name msgVariable} {
    upvar #0 $name c
    upvar 1 $msgVariable msg
    if {[catch {
        seek $c(fh) [expr {$c(endoffset) - 16}] start
        set fc [read $c(fh) 16]
    }]} {
        set msg "$::cookfs::pages::errorMessage: index not found"
        return 0
    }
    if {[string length $fc] != 16} {
        set msg "$::cookfs::pages::errorMessage: unable to read index suffix"
        return 0
    }
    if {[string range $fc 9 15] != "$c(cfsname)"} {
        set msg "$::cookfs::pages::errorMessage: invalid file signature"
        return 0
    }
    binary scan [string range $fc 0 7] II idxsize numpages

    set idxoffset [expr {$c(endoffset) - (16 + $idxsize + ($numpages * 20))}]
    set c(indexoffset) $idxoffset

    if {$idxoffset < 0} {
        set msg "$::cookfs::pages::errorMessage: page sizes not found"
        return 0
    }
    
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

    if {$c(hash) == "crc32"} {
        set md5 [string toupper [format %08x%08x%08x%08x \
            0 0 [string length $contents] [::cookfs::getCRC32 $contents] \
            ]]
    }  else  {
        set md5 [string toupper [md5::md5 -hex $contents]]
    }

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

    if {$c(haschanged) || $c(indexChanged)} {
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
        set eo [tell $c(fh)]
        if {$eo < $c(endoffset)} {
            catch {chan truncate $c(fh)}
        }
        set c(endoffset) $eo
        set c(haschanged) 0
        set c(indexChanged) 0
    }

    if {$c(fh) != ""} {
        close $c(fh)
        set c(fh) ""
    }
    
    return $c(endoffset)
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

proc cookfs::pages::sethash {name hash} {
    upvar #0 $name c
    set c(hash) $hash
}

proc cookfs::pages::setcachesize {name csize} {
    upvar #0 $name c
    if {![string is integer -strict $csize]} {
        error "Invalid cache size"
    }  elseif  {$csize < 0} {
        set csize 0
    }  elseif  {$csize > 256} {
        set csize 256
    }
    set c(cachelist) {}
    array unset c cache,*
    set c(cachesize) $csize
}

proc cookfs::pages::getFilesize {name} {
    upvar #0 $name c
    set o $c(startoffset)
    foreach i $c(idx.sizelist) {
        incr o $i
    }
    return $o
}

proc cookfs::pages::getCompression {name} {
    upvar #0 $name c
    return $c(compression)
}

proc cookfs::pages::setCompression {name compression} {
    upvar #0 $name c
    if {[catch {
        set c(cid) [::cookfs::pages::compression2cid $compression]
    } err]} {
        error $err $err
    }
    set c(compression) $compression
}

proc cookfs::pages::handle {name cmd args} {
    upvar #0 $name c
    switch -- $cmd {
        get {
            if {[llength $args] == 1} {
                if {[catch {
                    pageGet $name [lindex $args 0]
                } rc]} {
                    # TODO: log error
                    error "Unable to retrieve chunk"
                }
                return $rc
            }
        }
        add {
            if {[llength $args] == 1} {
                if {[catch {
                    pageAdd $name [lindex $args 0]
                } rc]} {
                    # TODO: log error
                    error "Unable to add page"
                }
                return $rc
            }
            
        }
        length {
            if {[llength $args] == 0} {
                return [llength $c(idx.sizelist)]
            }
            
        }
        index {
            if {[llength $args] == 1} {
                set c(indexdata) [lindex $args 0]
                set c(indexChanged) 1
                return ""
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
            return ""
        }
        close {
            return [cleanup $name]
        }
        hash {
            if {[llength $args] == 1} {
                sethash $name [lindex $args 0]
                return
            }
        }
        dataoffset {
            return $c(startoffset)
        }
        aside - gethead - getheadmd5 - gettail - gettailmd5 {
            error "Not implemented"
        }
        cachesize {
            if {[llength $args] == 1} {
                set csize [lindex $args 0]
                setcachesize $name $csize
            }
            return $c(cachesize)
        }
        filesize {
            if {[llength $args] == 0} {
                return [getFilesize $name]
            }
        }
        compression {
            if {[llength $args] == 0} {
                return [getCompression $name]
            }  elseif {[llength $args] == 1} {
                return [setCompression $name [lindex $args 0]]
            }
        }
        default {
            error "Not implemented"
        }
    }
    error "TODO: help"
}

package provide vfs::cookfs::tcl::pages 1.3.2
