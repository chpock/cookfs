# Pure Tcl implementation of pages
#
# (c) 2010-2014 Wojciech Kocjan

namespace eval cookfs {}
namespace eval cookfs::tcl {}
namespace eval cookfs::pages {}

set ::cookfs::pages::errorMessage "Unable to create Cookfs object"

set ::cookfs::pageshandleIdx 0
proc cookfs::tcl::pages {args} {
    set name ::cookfs::tcl::pageshandle[incr ::cookfs::pageshandleIdx]
    upvar #0 $name c
    array set c {
        readonly 0
        cachelist {}
        compression zlib
        compresscommand ""
        asynccompresscommand ""
        asyncdecompresscommand ""
        decompresscommand ""
        cachesize 8
        indexdata ""
        endoffset ""
        cfsname "CFS0002"
        lastop read
        hash crc32
        alwayscompress 0
        asyncwrites {}
        asyncpreloadBusy {}
    }

    if {[info commands ::zlib] != ""} {
        set c(_usezlib) 1
    }  else  {
        set c(_usezlib) 0
    }

    if {![catch { package require md5 2 }]} {
        set c(hash) md5
    }

    while {[llength $args] > 1} {
        switch -- [lindex $args 0] {
            -endoffset - -compression - -cachesize - -cfsname -
            -asynccompresscommand - -asyncdecompresscommand -
            -compresscommand - -decompresscommand {
                set c([string range [lindex $args 0] 1 end]) [lindex $args 1]
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
            -asyncdecompressqueuesize {
                set args [lrange $args 2 end]
                # ignored in Tcl version
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
            if {$fileSize > 65536} {
                seek $c(fh) -65536 current
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

    if {[::cookfs::pages::readIndex $name msg]} {
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

    if {([string length $c(asyncdecompresscommand)] != 0)} {
        set c(asyncpreloadCount) 2
    }

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
        if {$c(compresscommand) != ""} {
            set data "\u00ff[uplevel #0 [concat $c(compresscommand) [list $origdata]]]"
        }  else  {
            error "No compresscommand specified"
        }

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

proc cookfs::pages::pagewrite {name contents} {
    upvar #0 $name c

    binary scan $contents H* hd

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

    set idx [llength $c(idx.sizelist)]
    lappend c(idx.md5list) $md5

    if {($c(cid) == 255) && ([string length $c(asynccompresscommand)] != 0)} {
        lappend c(idx.sizelist) -1
        asyncCompress $name $idx $contents
    }  else  {
	# ensure no writes are in progress
	while {[asyncCompressWait $name true]} {}
        set contents [compress $name $contents]
        lappend c(idx.sizelist) [string length $contents]
        pagewrite $name $contents
    }

    return $idx
}

proc cookfs::pages::cleanup {name} {
    upvar #0 $name c
    while {[asyncCompressWait $name true]} {}
    while {[asyncDecompressWait $name -1 true]} {}
    asyncCompressFinalize $name
    asyncDecompressFinalize $name

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

proc cookfs::pages::pageGetOffset {name idx} {
    upvar #0 $name c
    set o $c(startoffset)
    for {set i 0} {$i < $idx} {incr i} {
        incr o [lindex $c(idx.sizelist) $i]
    }
    return $o
}

proc cookfs::pages::pageGetData {name idx} {
    upvar #0 $name c
    set o $c(startoffset)

    if {$idx >= [llength $c(idx.sizelist)]} {
        error "Out of range"
    }
    for {set i 0} {$i < $idx} {incr i} {
        incr o [lindex $c(idx.sizelist) $i]
    }
    seek $c(fh) $o start
    set fc [read $c(fh) [lindex $c(idx.sizelist) $idx]]
    set c(lastop) read

    return $fc
}

proc cookfs::pages::pageGetStored {name idx variableName {clean true}} {
    upvar $variableName fc
    upvar #0 $name c

    # pending writes
    if {[info exists c(asyncwrites,$idx)]} {
        set fc $c(asyncwrites,$idx)
        return true
    }

    if {[cacheGet $name $idx fc]} {
        return true
    }

    return [asyncGet $name $idx fc $clean]
}

proc cookfs::pages::cacheGet {name idx variableName} {
    upvar $variableName fc
    upvar #0 $name c

    set cidx [lsearch -exact $c(cachelist) $idx]
    if {$cidx >= 0} {
        if {$cidx > 0} {
            set c(cachelist) [linsert [lreplace $c(cachelist) $cidx $cidx] 0 $idx]
        }
        set fc $c(cache,$idx)
        return true
    }

    return false
}

proc cookfs::pages::cacheAdd {name idx fc} {
    upvar #0 $name c

    if {$c(cachesize) > 0} {
        set cidx [lsearch -exact $c(cachelist) $idx]
        if {$cidx < 0} {
            if {[llength $c(cachelist)] >= $c(cachesize)} {
                set remidx [lindex $c(cachelist) [expr {$c(cachesize) - 1}]]
                unset c(cache,$remidx)
                set c(cachelist) [lrange $c(cachelist) 0 [expr {$c(cachesize) - 2}]]
            }
            set c(cachelist) [linsert $c(cachelist) 0 $idx]
            set c(cache,$idx) $fc
        }
    }
}

proc cookfs::pages::pageGet {name idx} {
    asyncPreload $name [expr {$idx + 1}]

    if {![pageGetStored $name $idx fc]} {
        set fc [pageGetData $name $idx]
        set fc [decompress $name $fc]
        cacheAdd $name $idx $fc
    }

    return $fc
}

proc cookfs::pages::asyncCompress {name idx contents} {
    upvar #0 $name c
    set c(asyncwrites,$idx) $contents
    while {[asyncCompressWait $name false]} {}
    lappend c(asyncwrites) $idx
    uplevel #0 [concat $c(asynccompresscommand) [list process $idx $contents]]
}

proc cookfs::pages::asyncCompressWait {name require} {
    upvar #0 $name c
    if {([string length $c(asynccompresscommand)] != 0) && ($require || ([llength $c(asyncwrites)] > 0))} {
        if {[llength $c(asyncwrites)] > 0} {
            set widx [lindex $c(asyncwrites) 0]
        }  else  {
            set widx -1
        }
        set rc [uplevel #0 [concat $c(asynccompresscommand) [list wait $widx $require]]]
        if {[llength $rc] > 0} {
            set idx [lindex $rc 0]
            set contents [lindex $rc 1]
            if {$idx != [lindex $c(asyncwrites) 0]} {
                error "asyncCompressWait returned $idx, expecting [lindex $c(asyncwrites) 0]"
            }
            set origContents $c(asyncwrites,$idx)
            if {((!$c(alwayscompress)) && ([string length $contents] > [string length $origContents]))} {
                set contents \u0000${origContents}
            }  else  {
                set contents [binary format c $c(cid)]$contents
            }
            lset c(idx.sizelist) $idx [string length $contents]
            pagewrite $name $contents
            unset c(asyncwrites,$idx)
            set c(asyncwrites) [lrange $c(asyncwrites) 1 end]
            return [expr {[llength $c(asyncwrites)] > 0}]
        }  elseif {[llength $c(asyncwrites)] > 0} {
            return $require
        }  else  {
            return false
        }
    }  else  {
        return false
    }
}

proc cookfs::pages::asyncCompressFinalize {name} {
    upvar #0 $name c
    if {([string length $c(asynccompresscommand)] != 0)} {
        uplevel #0 [concat $c(asynccompresscommand) [list finalize {} {}]]
    }
}

proc cookfs::pages::asyncPreload {name idx} {
    upvar #0 $name c
    # TODO: clean old cache/buffers
    if {([string length $c(asyncdecompresscommand)] != 0)} {
        set maxIdx [expr {$idx + $c(asyncpreloadCount)}]
        if {$maxIdx >= [llength $c(idx.sizelist)]} {
            set maxIdx [llength $c(idx.sizelist)]
        }
        #puts "PRELOAD $idx -> $maxIdx -> $c(asyncpreloadBusy)"
        for {set i $idx} {$i < $maxIdx} {incr i} {
            if {![pageGetStored $name $i - false] && ([lsearch $c(asyncpreloadBusy) $i] < 0)} {
                set fc [pageGetData $name $i]
                # validate compression type and remove it before passing for processing
                if {[string index $fc 0] == "\u00ff"} {
                    #puts "PRELOAD  $i"
                    while {[asyncDecompressWait $name -1 false]} {}
                    set fc [string range $fc 1 end]
                    uplevel #0 [concat $c(asyncdecompresscommand) [list process $i $fc]]
                    lappend c(asyncpreloadBusy) $i
                }
            }
        }
        return true
    } else {
        return false
    }
}

proc cookfs::pages::asyncGet {name idx variableName {clean true}} {
    upvar #0 $name c
    upvar 1 $variableName fc
    # check if page is currently being processed
    if {$clean} {
        while {[lsearch -exact $c(asyncpreloadBusy) $idx] >= 0} {
            if {![asyncDecompressWait $name $idx false]} {
                # TODO: improve
                after 50
            }
        }
    }

    return [cacheGet $name $idx fc]
}

proc cookfs::pages::asyncDecompressWait {name widx require} {
    upvar #0 $name c
    if {([string length $c(asyncdecompresscommand)] != 0) && ($require || [llength $c(asyncpreloadBusy)] > 0)} {
	set rc [uplevel #0 [concat $c(asyncdecompresscommand) [list wait $widx $require]]]
	if {[llength $rc] > 0} {
	    set rcidx [lindex $rc 0]
	    #set c(asyncpreload,$rcidx) [lindex $rc 1]
            cacheAdd $name $rcidx [lindex $rc 1]
            #puts "WAIT GOT $rcidx"
	    if {[set lidx [lsearch -exact $c(asyncpreloadBusy) $rcidx]] >= 0} {
		set c(asyncpreloadBusy) [lreplace $c(asyncpreloadBusy) $lidx $lidx]
	    }
	    return [expr {[llength $c(asyncpreloadBusy)] > 0}]
	}  elseif {[llength $c(asyncpreloadBusy)] > 0} {
	    return $require
	}  else  {
	    return false
	}
    }  else  {
	return false
    }
}

proc cookfs::pages::asyncDecompressFinalize {name} {
    upvar #0 $name c
    if {([string length $c(asyncdecompresscommand)] != 0)} {
        uplevel #0 [concat $c(asyncdecompresscommand) [list finalize -1 1]]
    }
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
	set cid [::cookfs::pages::compression2cid $compression]
    } err]} {
        error $err $err
    }
    if {$cid != $c(cid)} {
	while {[asyncCompressWait $name true]} {}
	set c(cid) [::cookfs::pages::compression2cid $compression]
	set c(compression) $compression
    }
}

proc cookfs::pages::handle {name cmd args} {
    upvar #0 $name c
    switch -- $cmd {
        get {
            if {[llength $args] == 1} {
                if {[catch {
                    pageGet $name [lindex $args 0]
                } rc]} {
		    ::cookfs::debug {pageGet error: $::errorInfo}
                    error "Unable to retrieve chunk"
                }
                return $rc
            }
        }
        preload {
            if {[llength $args] == 1} {
                if {![asyncPreload $name [lindex $args 0]]} {
                    error "Unable to preload page"
                }
                return {}
            }
        }
        add {
            if {[llength $args] == 1} {
                if {[catch {
                    pageAdd $name [lindex $args 0]
                } rc]} {
		    ::cookfs::debug {pageAdd error: $::errorInfo}
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
            if {[llength $args] == 1} {
		return [pageGetOffset $name [lindex $args 0]]
	    }
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

package provide vfs::cookfs::tcl::pages 1.5.0
