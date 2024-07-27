# Handling of writing files to archive.
# Mainly used by close event handler of memchan, but can also be
# invoked separately to add files without writing them to channel first
#
# (c) 2010 Wojciech Kocjan, Pawel Salawa
# (c) 2024 Konstantin Kushnir

namespace eval cookfs {}
namespace eval cookfs::tcl {}
namespace eval cookfs::tcl::writer {
    variable writerhandleIdx 0
}

proc cookfs::tcl::writer {args} {
    set name ::cookfs::tcl::writer::handle[incr writer::writerhandleIdx]
    upvar #0 $name c
    array set c {
        pages ""
        index ""
        smallfilebufsize 0
        smallfilebuf ""
        smallfilepaths ""
        smallfilesize 0
        smallfilebuffersize 0
        pagesize 0
        writetomemory 0
    }
    while {[llength $args]} {

        set opt [lindex $args 0]
        set args [lrange $args 1 end]

        if { ![llength $args] } {
            return -code error "option \"$arg\" requires a value"
        }

        set val [lindex $args 0]
        set args [lrange $args 1 end]

        switch -- $opt {
            -pages -
            -index -
            -writetomemory -
            -smallfilebuffersize -
            -smallfilesize -
            -pagesize {
                set c([string range $opt 1 end]) $val
            }
            -pagesobject {
                set c(pages) $val
            }
            -fsindexobject {
                set c(index) $val
            }
            -smallfilebuffer {
                set c(smallfilebuffersize) $val
            }
            default {
                unset c
                return -code error "unknown option \"$opt\""
            }
        }

    }
    interp alias {} $name {} ::cookfs::tcl::writer::handle $name
    return $name
}

proc cookfs::tcl::writer::handle { name cmd args } {
    upvar #0 $name c
    switch -- $cmd {
        getbuf {
            if { [llength $args] != 1 } {
                error "wrong # args: should be \"$name\
                    $cmd index\""
            }
            set idx [lindex $args 0]
            if { $idx < 0 } {
                set idx [expr { -$idx - 1 }]
            }
            if { $idx >= [llength $c(smallfilebuf)] } {
                error "buffer index is out of range"
            }
            return [lindex $c(smallfilebuf) $idx]
        }
        smallfilebuffersize {
            if { [llength $args] } {
                error "wrong # args: should be \"$name $cmd\""
            }
            return $c(smallfilebufsize)
        }
        write {
            tailcall write $name {*}$args
        }
        deleteFile {
            tailcall deleteFile $name {*}$args
        }
        writetomemory {
            if { [llength $args] > 1 } {
                error "wrong # args: should be \"$name\
                    $cmd ?writetomemory?\""
            }
            if { [llength $args] } {
                set c(writetomemory) [lindex $args 0]
            }
            return $c(writetomemory)
        }
        index {
            if { [llength $args] > 1 } {
                error "wrong # args: should be \"$name\
                    $cmd ?fsindex?\""
            }
            if { [llength $args] } {
                set c(index) [lindex $args 0]
            }
            return $c(index)
        }
        purge {
            if { [llength $args] } {
                error "wrong # args: should be \"$name\ $cmd\""
            }
            tailcall purge $name
        }
        delete {
            purge $name
            unset $name
            rename $name ""
            return ""
        }
        close {
            purge $name
        }
        default {
            error "Not implemented"
        }
    }
}

# Purge a chunk of small files, internal proc for purge
proc cookfs::tcl::writer::_purgeSmallfilesChunk {} {
    # helper proc to dump current chunk
    upvar 1 c c
    upvar 1 currentchunk currentchunk
    upvar 1 currentchunkPOS currentchunkPOS

    if {[llength $currentchunkPOS] > 0} {
        set idx [$c(pages) add $currentchunk]

        foreach {pathlist offset size} $currentchunkPOS {
            foreach path $pathlist {
                set mtime [$c(index) getmtime $path]
                $c(index) set $path $mtime [list $idx $offset $size]
            }
        }

        set currentchunk ""
        set currentchunkPOS [list]
    }
}

# purge all small files pending write; usually called from finalize
# or when buffer size exceeds maximum buffer for large files
proc cookfs::tcl::writer::purge {wrid} {
    upvar #0 $wrid c
    if {!$c(writetomemory) && $c(smallfilebufsize) > 0} {
        set plist [list]

        # create complete list of files
        foreach {path size clk} $c(smallfilepaths) buf $c(smallfilebuf) {
            set pbuf($path) $buf
            set psize($path) $size
            set pclk($path) $clk
            lappend bufmap($buf) $path
        }

        set search [array startsearch bufmap]
        while {[array anymore bufmap $search]} {
	    set pathlist $bufmap([array next bufmap $search])
            set path [lindex $pathlist 0]
            lappend plist [list $pathlist [file extension $path]/[file tail $path]]
        }
	array donesearch bufmap $search

        set currentchunk ""
        set currentchunkPOS [list]

        # iterate over files, based on file's name ([file tail])
        foreach path [lsort -index 1 -dictionary $plist] {
            set pathlist [lindex $path 0]
            set path [lindex $pathlist 0]
            set size $psize($path)

            # if size of page would exceed maximum size,
            # write whatever is in buffer now
            if {([string length $currentchunk] + $size) > $c(pagesize)} {
                _purgeSmallfilesChunk
            }

            # append data to current chunk
            set offset [string length $currentchunk]
            lappend currentchunkPOS $pathlist $offset $size
            append currentchunk $pbuf($path)
        }

        # dump remaining files, if any
        _purgeSmallfilesChunk
    }
    array set c {
        smallfilepaths {}
        smallfilebuf {}
        smallfilebufsize 0
    }
}

proc cookfs::tcl::writer::deleteFile {wrid args} {
    upvar #0 $wrid c
    # args - path type data size
    foreach path_to_delete $args {
        set idx 0
        set deleted 0
        set newsmallfilepaths [list]
        foreach { path size clk } $c(smallfilepaths) {
            if { !$deleted } {
                if { $path eq $path_to_delete } {
                    set c(smallfilebuf) [lreplace $c(smallfilebuf) $idx $idx]
                    set c(smallfilebufsize) [expr { $c(smallfilebufsize) - $size }]
                    set deleted 1
                } else {
                    lappend newsmallfilepaths $path $size $clk
                    incr idx
                }
            } else {
                # shift buffer number for other files
                set entry [$c(index) get $path]
                set chunklist [lindex $entry 2]
                set chunklist [lreplace $chunklist 0 0 [expr { [lindex $chunklist 0] + 1 }]]
                $c(index) set $path [lindex $entry 0] $chunklist
                set entry [$c(index) get $path]
                lappend newsmallfilepaths $path $size $clk
            }
        }
        if { $deleted } {
            set c(smallfilepaths) $newsmallfilepaths
        }
    }
}

proc cookfs::tcl::writer::write {wrid args} {
    upvar #0 $wrid c
    # args - path type data size

    # iterate over arguments
    foreach {path datatype data size} $args {
        deleteFile $wrid $path
        if {$size == ""} {
            # read actual size, from file or channel
            switch -- $datatype {
                file {
                    set size [file size $data]
                }
                channel {
                    set tmp [tell $data]
                    seek $data 0 end
                    set size [tell $data]
                    seek $data $tmp start
                }
                data {
                    set size [string length $data]
                }
                default {
                    error "Unknown data type: $datatype"
                }
            }
        }

        # get actual channel and whether it should be closed
        switch -- $datatype {
            file {
                set clk [file mtime $data]
                set chan [open $data r]
                fconfigure $chan -translation binary
                set doclose 1
                set rawdata 0
            }
            channel {
                set clk [clock seconds]
                set chan $data
                set translation [fconfigure $chan -translation]
                set encoding [fconfigure $chan -encoding]
                fconfigure $chan -translation binary
                set doclose 0
                set rawdata 0
            }
            data {
                set clk [clock seconds]
                set remainingdata $data
                set doclose 0
                set rawdata 1
            }
            default {
                error "Unknown data type: $datatype"
            }
        }

        # check if this is a small file or writing to memory has been enabled
        if {($size <= $c(smallfilesize)) || $c(writetomemory)} {
            # add file to "small file buffer" instead of writing to disk
            if {$rawdata} {
                set fc $data
            }  else  {
                set fc [read $chan]
            }

            set sfidx [llength $c(smallfilebuf)]

            if { [catch {
                $c(index) set $path $clk [list [expr {-$sfidx - 1}] 0 $size]
            } res opts] } {
                if { $doclose } {
                    close $chan
                } elseif { $datatype eq "channel" } {
                    fconfigure $chan -translation $translation -encoding $encoding
                }
                return -options $opts "unable to add \"$path\": $res"
            }

            lappend c(smallfilebuf) $fc
            lappend c(smallfilepaths) $path $size $clk
            incr c(smallfilebufsize) [string length $fc]

            # if current buffer exceeds maximum, write small files to clean it
            # but only if not writing to memory
            if {(!$c(writetomemory)) && ($c(smallfilebufsize) >= $c(smallfilebuffersize))} {
                purge $wrid
            }
        }  else  {
            # large file - written as chunks
            set left $size
            set chunklist [list]

            while {$left > 0} {
                if {$left > $c(pagesize)} {
                    set read $c(pagesize)
                }  else  {
                    set read $left
                }

                if {$rawdata} {
                    set data [string range $remainingdata 0 [expr {$read - 1}]]
                    set remainingdata [string range $remainingdata $read end]
                }  else  {
                    set data [read $chan $read]
                }
                set datalen [string length $data]

                if {$datalen == 0} {break}

                set chunkid [$c(pages) add $data]

                lappend chunklist $chunkid 0 $datalen

                incr left -$datalen
            }

            $c(index) set $path $clk $chunklist
        }
        if { $doclose } {
            close $chan
        } elseif { $datatype eq "channel" } {
            fconfigure $chan -translation $translation -encoding $encoding
        }
    }
}

interp alias {} cookfs::writer {} cookfs::tcl::writer

package provide cookfs::tcl::writer 1.9.0
