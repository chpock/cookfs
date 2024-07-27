# Pure Tcl implementation of pages
#
# (c) 2010-2014 Wojciech Kocjan
# (c) 2024 Konstantin Kushnir

namespace eval cookfs {}
namespace eval cookfs::tcl {}
namespace eval cookfs::tcl::pages {
    variable errorMessage "Unable to create Cookfs object"
    variable pageshandleIdx 0
}

# enable bzip2 if Trf package is available
if { ![catch { package require Trf }] } {
    set ::cookfs::pkgconfig::pkgconfig(feature-bzip2) 1
}

proc cookfs::tcl::pages {args} {
    variable default_hash

    set name ::cookfs::tcl::pages::handle[incr pages::pageshandleIdx]
    upvar #0 $name c
    array set c {
        firstwrite 0
        readonly 0
        cachelist {}
        compression {}
        base_compression {}
        compresscommand ""
        asynccompresscommand ""
        asyncdecompresscommand ""
        decompresscommand ""
        cachesize 8
        indexdata ""
        endoffset ""
        cfsname "CFS0003"
        cfsstamp "CFSS003"
        lastop read
        hash crc32
        alwayscompress 0
        asyncwrites {}
        asyncpreloadBusy {}
        maxAge 50
    }

    if {[info commands ::zlib] != ""} {
        set c(_usezlib) 1
    }  else  {
        set c(_usezlib) 0
    }

    # Use md5 hash only when it is available.
    # The command "package require" command is very expensive because it tries
    # to load packages by parsing pkgIndex.tcl files. It takes a long time.
    # To avoid delays in creating each page object, we will cache the
    # availability of the md5 packet in the "default_hash" variable.
    if { [info exists default_hash] } {
        set c(hash) $default_hash
    } else {
        if {![catch { package require md5 2 }]} {
            set c(hash) md5
            set default_hash md5
        } else {
            set default_hash crc32
        }
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

    if {[llength $args] != 1} {
        error "No filename provided"
    }

    if { $c(compression) ne "" } {
        if {[catch {
            set c(cid) [pages::compression2cid $c(compression)]
            set c(cid_base) $c(cid)
        } err]} {
            error $err $err
        }
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
        set msg "$pages::errorMessage: $err"
        error $msg $msg
    }

    if {$c(endoffset) != ""} {
        if {[catch {
            seek $c(fh) $c(endoffset) start
        }]} {
            set msg "$pages::errorMessage: index not found"
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
            set msg "$pages::errorMessage: index not found"
            error $msg $msg
        }
        set tailOffset [string last $c(cfsname) $tail]
        if {$tailOffset >= 0} {
            set c(endoffset) [expr {[tell $c(fh)] - [string length $tail] + $tailOffset + 7}]
            seek $c(fh) $c(endoffset) start
        }  else  {
            set c(endoffset) [tell $c(fh)]
            set msg "$pages::errorMessage: signature not found"
        }
    }

    if {![info exists msg] && [pages::readIndex $name msg]} {
        set c(haschanged) 0
        set c(indexChanged) 0
    }  else  {
        if {$c(readonly)} {
            if { [catch { pages::searchstamp $name } size] } {
                set size -1
            }
            catch {close $c(fh)}
            if { $size > 0 } {
                set msg "The archive appears to be corrupted or truncated.\
                    Expected archive size is $size bytes or larger."
            }
            error $msg $msg
        }
        set c(firstwrite) 1
        set c(startoffset) $c(endoffset)
        set c(haschanged) 1
        set c(indexChanged) 1
        set c(idx.md5list) {}
        set c(idx.sizelist) {}
        set c(idx.comprlist) {}
        set c(idx.comprlevellist) {}
        set c(idx.encryptionlist) {}
        set c(idx.sizeoriglist) {}
    }

    if { ![info exists c(cid_base)] } {
        set c(cid_base) [pages::compression2cid "zlib"]
    }

    if { ![info exists c(cid)] } {
        set c(cid) $c(cid_base)
    }

    interp alias {} $name {} ::cookfs::tcl::pages::handle $name

    if {([string length $c(asyncdecompresscommand)] != 0)} {
        set c(asyncpreloadCount) 2
    }

    return $name
}

proc cookfs::tcl::pages::crc32 { v } {
    variable crc32use_zlib
    if { ![info exists crc32use_zlib] } {
        if {(![catch {zlib crc32 ""} testvalue]) && ($testvalue == "0")} {
            set crc32use_zlib 1
        } else {
            set crc32use_zlib 0
        }
    }
    if { $crc32use_zlib } {
        tailcall zlib crc32 $v
    }
    package require crc32
    tailcall crc::crc32 -format %d $v
}

proc cookfs::tcl::pages::compress {name origdata} {
    upvar #0 $name c

    set cid $c(cid)
    set level 0

    if {[string length $origdata] == 0} {
        return [list $cid $level ""]
    }

    if {$cid == 1} {
        if {$c(_usezlib)} {
            set data [zlib deflate $origdata]
        }  else  {
            set data [vfs::zip -mode compress -nowrap 1 $origdata]
        }
    }  elseif {$cid == 2} {
        package require Trf
        set data [bz2 -mode compress $origdata]
    } elseif {$cid == 3} {
        error "Lzma compression is not supported by Tcl pages"
    } elseif {$cid == 4} {
        error "zstd compression is not supported by Tcl pages"
    } elseif {$cid == 5} {
        error "brotli compression is not supported by Tcl pages"
    }  elseif {$cid == 255} {
        if {$c(compresscommand) != ""} {
            set data [uplevel #0 [concat $c(compresscommand) [list $origdata]]]
        }  else  {
            error "No compresscommand specified"
        }

    }
    # if compression algorithm was not matched or
    #   we should not always compress and compressed data is not smaller, revert to uncompressed data
    if {(![info exists data]) || ((!$c(alwayscompress)) && ([string length $data] > [string length $origdata]))} {
        set data $origdata
        set cid 0
    }
    return [list $cid $level $data]
}

proc cookfs::tcl::pages::decompress {name cid sizeUncompress data} {
    upvar #0 $name c
    if {[string length $data] == 0} {
        return ""
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
            return [bz2 -mode decompress $data]
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

proc cookfs::tcl::pages::compression2cid {name} {
    set name [split $name :]
    set level [lindex $name 1]
    set name [lindex $name 0]
    if { [string length $level] } {
        if { ![string is integer -strict $level] } {
            return -code error "expected integer but got \"$level\""
        } elseif { $level < -1 || $level > 255 } {
            return -code error "the compression level is expected to be\
                an integer between -1 and 255, but got \"$level\""
        }
    }
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
        lzma {
            return 3
        }
        zstd {
            return 4
        }
        brotli {
            return 5
        }
        custom {
            return 255
        }
        default {
            error "Unknown compression \"$name\""
        }
    }
}

proc cookfs::tcl::pages::cid2compression {name} {
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
        3 {
            return "lzma"
        }
        4 {
            return "zstd"
        }
        5 {
            return "brotli"
        }
        255 {
            return "custom"
        }
        default {
            error "Unknown compression id \"$name\""
        }
    }
}

proc cookfs::tcl::pages::unsign_list { list } {
    set result [list]
    foreach x $list {
        lappend result [expr { $x & 0xff }]
    }
    return $result
}

proc cookfs::tcl::pages::readIndex {name msgVariable} {
    variable errorMessage
    upvar #0 $name c
    upvar 1 $msgVariable msg
    set suffix_size 62
    if {[catch {
        seek $c(fh) [expr {$c(endoffset) - $suffix_size}] start
        set fc [read $c(fh) $suffix_size]
    }]} {
        set msg "$errorMessage: index not found"
        return 0
    }
    if {[string length $fc] != $suffix_size} {
        set msg "$errorMessage: unable to read index suffix"
        return 0
    }

    binary scan $fc cccccH32IIccH32IIa* \
        base_compression \
        base_compressionLevel \
        encryption \
        pgindex_compression \
        pgindex_compressionLevel \
        pgindex_md5 \
        pgindex_sizeCompressed \
        pgindex_sizeUncompressed \
        fsindex_compression \
        fsindex_compressionLevel \
        fsindex_md5 \
        fsindex_sizeCompressed \
        fsindex_sizeUncompressed \
        signature

    set pgindex_md5 [string toupper $pgindex_md5]
    set fsindex_md5 [string toupper $fsindex_md5]

    if {$signature ne $c(cfsname)} {
        set msg "$errorMessage: signature not found"
        return 0
    }

    if { ($pgindex_sizeCompressed + $fsindex_sizeCompressed + $suffix_size) > $c(endoffset) } {
        set msg "$errorMessage: failed to read index"
        return 0
    }

    if { $pgindex_sizeCompressed + $fsindex_sizeCompressed > 0 } {
        seek $c(fh) [expr {$c(endoffset) - $suffix_size - $pgindex_sizeCompressed - $fsindex_sizeCompressed}] start
    }

    set c(idx.md5list) {}
    set c(idx.sizelist) {}
    set c(idx.comprlist) {}
    set c(idx.comprlevellist) {}
    set c(idx.sizeoriglist) {}
    set c(idx.encryptionlist) {}

    if { $pgindex_sizeCompressed > 0 } {

        set fc [read $c(fh) $pgindex_sizeCompressed]
        set fc [decompress $name $pgindex_compression $pgindex_sizeUncompressed $fc]

        binary scan [string range $fc 0 3] I numpages

        binary scan [string range $fc 4 end] "c${numpages}c${numpages}c${numpages}I${numpages}I${numpages}a*" \
            c(idx.comprlist) c(idx.comprlevellist) c(idx.encryptionlist) \
            c(idx.sizelist) c(idx.sizeoriglist) md5data

        set c(idx.comprlist) [unsign_list $c(idx.comprlist)]
        set c(idx.comprlevellist) [unsign_list $c(idx.comprlevellist)]
        set c(idx.encryptionlist) [unsign_list $c(idx.encryptionlist)]

        binary scan $md5data H* md5hex
        set md5hex [string toupper $md5hex]
        for {set i 0} {$i < $numpages} {incr i} {
            lappend c(idx.md5list) [string range $md5hex 0 31]
            set md5hex [string range $md5hex 32 end]
        }

    }

    if { $fsindex_sizeCompressed > 0 } {

        set fc [read $c(fh) $fsindex_sizeCompressed]
        set c(indexdata) [decompress $name $fsindex_compression $fsindex_sizeUncompressed $fc]

    } else {
        set c(indexdata) {}
    }

    set idxoffset [expr {$c(endoffset) - $suffix_size - $pgindex_sizeCompressed - $fsindex_sizeCompressed}]
    set c(indexoffset) $idxoffset

    foreach s $c(idx.sizelist) {
        incr idxoffset -$s
    }
    set c(startoffset) $idxoffset

    set c(cid_base) $base_compression

    return 1
}

proc cookfs::tcl::pages::searchstamp {name} {
    upvar #0 $name c
    set buffer ""
    # read by 512kb chunks
    set chunk 524288
    # max read 10mb
    set maxread 10485760
    set read 0
    seek $c(fh) 0 start
    while { ![eof $c(fh)] && $read < $maxread } {
        set initialbufsize [string length $buffer]
        append buffer [read $c(fh) $chunk]
        incr read [expr { [string length $buffer] - $initialbufsize }]
        if { [set pos [string first $c(cfsstamp) $buffer]] == -1 } {
            set buffer [string range $buffer end-20 end]
            continue
        }
        incr pos [string length $c(cfsstamp)]
        binary scan [string range $buffer $pos $pos+8] W size
        return $size
    }
    return -1
}

proc cookfs::tcl::pages::addstamp {name size} {
    upvar #0 $name c
    set stamp "$c(cfsstamp)[binary format W $size]"
    if { $size == 0 } {
        if { !$c(firstwrite) } {
            return
        }
        seek $c(fh) 0 end
        puts -nonewline $c(fh) $stamp
        incr c(startoffset) [string length $stamp]
        set c(firstwrite) 0
    } else {
        seek $c(fh) [expr { $c(startoffset) - [string length $stamp] }] start
        puts -nonewline $c(fh) $stamp
    }
}

proc cookfs::tcl::pages::pagewrite {name contents} {
    upvar #0 $name c
    if {!$c(haschanged)} {
        seek $c(fh) $c(indexoffset) start
    }  else  {
        # TODO: optimize not to seek in subsequent writes
        seek $c(fh) 0 end
    }
    if {[catch {
        addstamp $name 0
        puts -nonewline $c(fh) $contents
    }]} {
        error "Unable to add page"
    }
    set c(haschanged) 1
}

proc cookfs::tcl::pages::getHash {name contents} {
    upvar #0 $name c
    if {$c(hash) == "crc32"} {
        set md5 [string toupper [format %08x%08x%08x%08x \
            0 0 [string length $contents] [crc32 $contents] \
            ]]
    }  else  {
        set md5 [string toupper [md5::md5 -hex $contents]]
    }
    return $md5
}

proc cookfs::tcl::pages::pageAdd {name contents} {
    upvar #0 $name c

    set md5 [getHash $name $contents]

    set idx 0
    foreach imd5 $c(idx.md5list) {
        if {[string equal $md5 $imd5]} {
            if {[string equal [pageGet $name $idx -1000] $contents]} {
                return $idx
            }
        }
        incr idx
    }

    set idx [llength $c(idx.sizelist)]
    lappend c(idx.md5list) $md5
    lappend c(idx.sizeoriglist) [string length $contents]
    lappend c(idx.encryptionlist) 0

    if {($c(cid) == 255) && ([string length $c(asynccompresscommand)] != 0)} {
        lappend c(idx.sizelist) -1
        asyncCompress $name $idx $contents
    }  else  {
	# ensure no writes are in progress
	while {[asyncCompressWait $name true]} {}
	lassign [compress $name $contents] cid level contents
        lappend c(idx.sizelist) [string length $contents]
        lappend c(idx.comprlist) $cid
        lappend c(idx.comprlevellist) $level
        pagewrite $name $contents
    }

    return $idx
}

proc cookfs::tcl::pages::cleanup {name} {
    upvar #0 $name c
    while {[asyncCompressWait $name true]} {}
    while {[asyncDecompressWait $name -1 true]} {}
    asyncCompressFinalize $name
    asyncDecompressFinalize $name

    if {$c(haschanged) || $c(indexChanged)} {
        addstamp $name 0
        set offset $c(startoffset)
        foreach i $c(idx.sizelist) {
            incr offset $i
        }
        seek $c(fh) $offset start

        set base_compression $c(cid)
        set base_compressionLevel 0
        set encryption 0

        # Make sure we use base compression and compression level for
        # pgindex/fsindex data
        set c(cid) $c(cid_base)

        if { [llength $c(idx.sizelist)] } {

            set pgindex ""
            append pgindex [binary format I [llength $c(idx.sizelist)]]
            append pgindex [binary format c* $c(idx.comprlist)]
            append pgindex [binary format c* $c(idx.comprlevellist)]
            append pgindex [binary format c* $c(idx.encryptionlist)]
            append pgindex [binary format I* $c(idx.sizelist)]
            append pgindex [binary format I* $c(idx.sizeoriglist)]
            append pgindex [binary format H* [join $c(idx.md5list) ""]]

            set pgindex_sizeUncompressed [string length $pgindex]

            set pgindex_md5 [getHash $name $pgindex]

            lassign [compress $name $pgindex] pgindex_compression pgindex_compressionLevel pgindex

            set pgindex_sizeCompressed [string length $pgindex]

            puts -nonewline $c(fh) $pgindex

        } else {
            set pgindex_compression 0
            set pgindex_compressionLevel 0
            set pgindex_sizeUncompressed 0
            set pgindex_sizeCompressed 0
            set pgindex_md5 [string repeat "0" 32]
        }

        set fsindex_sizeUncompressed [string length $c(indexdata)]

        if { $fsindex_sizeUncompressed } {

            set fsindex_md5 [getHash $name $c(indexdata)]

            lassign [compress $name $c(indexdata)] fsindex_compression fsindex_compressionLevel fsindex

            set fsindex_sizeCompressed [string length $fsindex]

            puts -nonewline $c(fh) $fsindex

        } else {
            set fsindex_compression 0
            set fsindex_compressionLevel 0
            set fsindex_sizeCompressed 0
            set fsindex_md5 [string repeat "0" 32]
        }

        set suffix [binary format cccccH32IIccH32IIa* \
            $base_compression \
            $base_compressionLevel \
            $encryption \
            $pgindex_compression \
            $pgindex_compressionLevel \
            $pgindex_md5 \
            $pgindex_sizeCompressed \
            $pgindex_sizeUncompressed \
            $fsindex_compression \
            $fsindex_compressionLevel \
            $fsindex_md5 \
            $fsindex_sizeCompressed \
            $fsindex_sizeUncompressed \
            $c(cfsname)]

        puts -nonewline $c(fh) $suffix

        set eo [tell $c(fh)]
        if {$eo < $c(endoffset)} {
            catch {chan truncate $c(fh)}
        }
        addstamp $name $eo
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

proc cookfs::tcl::pages::pageGetOffset {name idx} {
    upvar #0 $name c
    set o $c(startoffset)
    for {set i 0} {$i < $idx} {incr i} {
        incr o [lindex $c(idx.sizelist) $i]
    }
    return $o
}

proc cookfs::tcl::pages::pageGetData {name idx} {
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

proc cookfs::tcl::pages::pageGetStored {name idx weight variableName {clean true}} {
    upvar $variableName fc
    upvar #0 $name c

    # pending writes
    if {[info exists c(asyncwrites,$idx)]} {
        set fc $c(asyncwrites,$idx)
        return true
    }

    if {[cacheGet $name $idx $weight fc]} {
        return true
    }

    return [asyncGet $name $idx fc $clean]
}

proc cookfs::tcl::pages::cacheGet {name idx args} {

    if { [llength $args] == 1 } {
        set weight ""
        set variableName [lindex $args 0]
    } elseif { [llength $args] == 2 } {
        set weight [lindex $args 0]
        set variableName [lindex $args 1]
    } else {
        error "cacheGet: wrong # args: \"$args\""
    }

    upvar $variableName fc
    upvar #0 $name c

    set cidx [lsearch -exact -index 0 $c(cachelist) $idx]

    if {$cidx >= 0} {
        cacheMoveToTop $name $cidx $weight
        # now our page is at the top
        set fc $c(cache,0)
        return true
    }

    return false

}

proc cookfs::tcl::pages::isCached {name idx} {
    upvar #0 $name c
    return [expr { [lsearch -exact -index 0 $c(cachelist) $idx] != -1 }]
}

proc cookfs::tcl::pages::cacheAdd {name idx weight fc} {
    upvar #0 $name c

    if {$c(cachesize) <= 0} {
        return
    }

    set cidx [lsearch -exact -index 0 $c(cachelist) $idx]

    if { $cidx != -1 } {
        cacheMoveToTop $name $cidx $weight
        return
    }

    if { [llength $c(cachelist)] && [llength $c(cachelist)] >= $c(cachesize) } {

        set newIdx [expr { [llength $c(cachelist)] - 1 }]

        for { set i [expr { [llength $c(cachelist)] - 2 }] } { $i >= 0 } { incr i -1 } {

            if { [lindex $c(cachelist) [list $i 1]] > [lindex $c(cachelist) [list $newIdx 1]] } {
                continue
            }

            if {
                [lindex $c(cachelist) [list $i 1]] == [lindex $c(cachelist) [list $newIdx 1]] &&
                [lindex $c(cachelist) [list $i 2]] <= [lindex $c(cachelist) [list $newIdx 2]]
            } {
                continue
            }

            set newIdx $i

        }

    } else {
        set newIdx [llength $c(cachelist)]
    }

    set c(cachelist) [lreplace $c(cachelist) $newIdx $newIdx [list $idx $weight 0]]
    set c(cache,$newIdx) $fc

    cacheMoveToTop $name $newIdx $weight
}

proc cookfs::tcl::pages::cacheMoveToTop {name idx {weight {}} } {
    upvar #0 $name c

    set rec [lindex $c(cachelist) $idx]
    if { [string length $weight] } {
        set rec [lreplace $rec 1 1 $weight]; # weight
    }
    set rec [lreplace $rec 2 2 0]; # age

    if { !$idx } {
        # avoid unnecessary action if the situation does not change
        set c(cachelist) [lreplace $c(cachelist) 0 0 $rec]
    } else {
        set c(cachelist) [linsert [lreplace $c(cachelist) $idx $idx] 0 $rec]
        set tmp $c(cache,$idx)
        for { set i $idx } { $i >= 1 } { incr i -1 } {
            set c(cache,$i) $c(cache,[expr { $i - 1 }])
        }
        set c(cache,0) $tmp
    }

}

proc cookfs::tcl::pages::tickTock {name} {
    upvar #0 $name c
    for { set i 0 } { $i < [llength $c(cachelist)] } { incr i } {
        set age [lindex $c(cachelist) [list $i 2]]
        incr age
        lset c(cachelist) [list $i 2] $age
        if { $age >= $c(maxAge) } {
            lset c(cachelist) [list $i 1] 0
        }
    }
}

proc cookfs::tcl::pages::pageGet {name idx weight} {
    upvar #0 $name c
    asyncPreload $name [expr {$idx + 1}]

    if {![pageGetStored $name $idx $weight fc] && ![cacheGet $name $idx $weight fc]} {
        set fc [pageGetData $name $idx]
        set fc [decompress $name [lindex $c(idx.comprlist) $idx] [lindex $c(idx.sizeoriglist) $idx] $fc]
        cacheAdd $name $idx $weight $fc
    }

    return $fc
}

proc cookfs::tcl::pages::asyncCompress {name idx contents} {
    upvar #0 $name c
    set c(asyncwrites,$idx) $contents
    while {[asyncCompressWait $name false]} {}
    lappend c(asyncwrites) $idx
    uplevel #0 [concat $c(asynccompresscommand) [list process $idx $contents]]
}

proc cookfs::tcl::pages::asyncCompressWait {name require} {
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
            set cid $c(cid)
            set level 0
            if {((!$c(alwayscompress)) && ([string length $contents] > [string length $origContents]))} {
                set contents $origContents
                set cid 0
            }
            lset c(idx.sizelist) $idx [string length $contents]
            lset c(idx.comprlist) $idx $cid
            lset c(idx.comprlevellist) $idx $level
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

proc cookfs::tcl::pages::asyncCompressFinalize {name} {
    upvar #0 $name c
    if {([string length $c(asynccompresscommand)] != 0)} {
        uplevel #0 [concat $c(asynccompresscommand) [list finalize {} {}]]
    }
}

proc cookfs::tcl::pages::asyncPreload {name idx} {
    upvar #0 $name c
    # TODO: clean old cache/buffers
    if {([string length $c(asyncdecompresscommand)] != 0)} {
        set maxIdx [expr {$idx + $c(asyncpreloadCount)}]
        if {$maxIdx >= [llength $c(idx.sizelist)]} {
            set maxIdx [llength $c(idx.sizelist)]
        }
        #puts "PRELOAD $idx -> $maxIdx -> $c(asyncpreloadBusy)"
        for {set i $idx} {$i < $maxIdx} {incr i} {
            if {![pageGetStored $name $i 1000 - false] && ([lsearch $c(asyncpreloadBusy) $i] < 0)} {
                set fc [pageGetData $name $i]
                # validate compression type
                if {[lindex $c(idx.comprlist) $idx] == 255} {
                    #puts "PRELOAD  $i"
                    while {[asyncDecompressWait $name -1 false]} {}
                    uplevel #0 [concat $c(asyncdecompresscommand) [list process $i $fc]]
                    lappend c(asyncpreloadBusy) $i
                } else {
                    #puts "WARNING! not custom compression for page #$idx - \"[lindex $c(idx.comprlist) $idx]\""
                }
            }
        }
        return true
    } else {
        return false
    }
}

proc cookfs::tcl::pages::asyncGet {name idx variableName {clean true}} {
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

proc cookfs::tcl::pages::asyncDecompressWait {name widx require} {
    upvar #0 $name c
    if {([string length $c(asyncdecompresscommand)] != 0) && ($require || [llength $c(asyncpreloadBusy)] > 0)} {
	set rc [uplevel #0 [concat $c(asyncdecompresscommand) [list wait $widx $require]]]
	if {[llength $rc] > 0} {
	    set rcidx [lindex $rc 0]
	    #set c(asyncpreload,$rcidx) [lindex $rc 1]
            cacheAdd $name $rcidx 1000 [lindex $rc 1]
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

proc cookfs::tcl::pages::asyncDecompressFinalize {name} {
    upvar #0 $name c
    if {([string length $c(asyncdecompresscommand)] != 0)} {
        uplevel #0 [concat $c(asyncdecompresscommand) [list finalize -1 1]]
    }
}

proc cookfs::tcl::pages::sethash {name args} {
    upvar #0 $name c
    if { [llength $args] } {
        set hash [lindex $args 0]
        if { $hash ni {md5 crc32} } {
            return -code error "bad hash \"$hash\": must be md5 or crc32"
        }
        set c(hash) $hash
    }
    return $c(hash)
}

proc cookfs::tcl::pages::setcachesize {name csize} {
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

proc cookfs::tcl::pages::getFilesize {name} {
    upvar #0 $name c
    set o $c(startoffset)
    foreach i $c(idx.sizelist) {
        incr o $i
    }
    return $o
}

proc cookfs::tcl::pages::getCompression {name} {
    upvar #0 $name c
    return $c(compression)
}

proc cookfs::tcl::pages::setCompression {name compression} {
    upvar #0 $name c
    if {[catch {
	set cid [compression2cid $compression]
    } err]} {
        error $err $err
    }
    if {$cid != $c(cid)} {
	while {[asyncCompressWait $name true]} {}
	set c(cid) [compression2cid $compression]
	set c(compression) $compression
    }
}

proc cookfs::tcl::pages::handle {name cmd args} {
    upvar #0 $name c
    switch -- $cmd {
        get {
            if {[llength $args] != 1 && [llength $args] != 3 } {
                error "wrong # args: should be \"$name\
                    $cmd ?-weight weight? index\""
            }
            set weight 0
            if { [llength $args] == 3 } {
                if {[lindex $args 0] ne "-weight"} {
                    error [format "unknown option \"%s\" has been\
                        specified where -weight is expected" [lindex $args 0]]
                }
                if {![string is integer -strict [lindex $args 1]]} {
                    error [format "expected integer but got\
                        \"%s\"" [lindex $args 1]]
                }
                set weight [lindex $args 1]
            }
            if {![string is integer -strict [lindex $args end]]} {
                error [format "expected integer but got\
                    \"%s\"" [lindex $args end]]
            }
            if {[catch {
                pageGet $name [lindex $args end] $weight
            } rc]} {
                ::cookfs::debug {pageGet error: $::errorInfo}
                error "Unable to retrieve chunk: $rc"
            }
            return $rc
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
                return [pageAdd $name [lindex $args 0]]
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
            if {![llength $args]} {
                return [sethash $name]
            } elseif {[llength $args] == 1} {
                return [sethash $name [lindex $args 0]]
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
        ticktock {
            if {[llength $args] > 1} {
                error "wrong # args: should be \"$name\ $cmd ?maxAge?\""
            }
            if {[llength $args]} {
                if {![string is integer -strict [lindex $args 0]]} {
                    error [format "expected integer but got\
                        \"%s\"" [lindex $args 0]]
                }
                if { [lindex $args 0] >= 0 } {
                    set c(maxAge) [lindex $args 0]
                }
            } else {
                tickTock $name
            }
            return $c(maxAge)
        }
        getcache {
            if {[llength $args] > 1} {
                error "wrong # args: should be \"$name\ $cmd ?index?\""
            }
            if {[llength $args]} {
                if {![string is integer -strict [lindex $args 0]]} {
                    error [format "expected integer but got\
                        \"%s\"" [lindex $args 0]]
                }
                return [isCached $name [lindex $args 0]]
            }
            return [lmap x $c(cachelist) { dict create {*}[list \
                index  [lindex $x 0] \
                weight [lindex $x 1] \
                age    [lindex $x 2] \
            ] }]
        }
        default {
            error "Not implemented"
        }
    }
    error "TODO: help"
}

if { ![llength [info commands cookfs::pages]] } {
    interp alias {} cookfs::pages {} cookfs::tcl::pages
}

package provide cookfs::tcl::pages 1.9.0
