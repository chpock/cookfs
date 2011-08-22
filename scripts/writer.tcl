# Handling of writing files to archive.
# Mainly used by close event handler of memchan, but can also be
# invoked separately to add files without writing them to channel first
#
# (c) 2010 Wojciech Kocjan, Pawel Salawa

namespace eval cookfs {}

package require vfs

# Purge a chunk of small files, internal proc for purgeSmallfiles
proc cookfs::_purgeSmallfilesChunk {} {
    # helper proc to dump current chunk
    upvar 1 fs fs fsid fsid
    upvar 1 currentchunk currentchunk
    upvar 1 currentchunkPOS currentchunkPOS

    if {[llength $currentchunkPOS] > 0} {
        set idx [$fs(pages) add $currentchunk]
        
        foreach {pathlist offset size} $currentchunkPOS {
            incr fs(changeCount)
            foreach path $pathlist {
                set mtime [$fs(index) getmtime $path]
                $fs(index) set $path $mtime [list $idx $offset $size]
            }
        }
        
        set currentchunk ""
        set currentchunkPOS [list]
    }
}

# purge all small files pending write; usually called from finalize
# or when buffer size exceeds maximum buffer for large files
proc cookfs::purgeSmallfiles {fsid} {
    upvar #0 $fsid fs

    if {$fs(smallfilebufsize) > 0} {
        set plist [list]

        # create complete list of files
        foreach {path size clk} $fs(smallfilepaths) buf $fs(smallfilebuf) {
            set pbuf($path) $buf
            set psize($path) $size
            set pclk($path) $clk
            lappend bufmap($buf) $path
        }

        foreach {buf pathlist} [array get bufmap] {
            set path [lindex $pathlist 0]
            lappend plist [list $pathlist [file extension $path]/[file tail $path]]
        }

        set currentchunk ""
        set currentchunkPOS [list]

        # iterate over files, based on file's name ([file tail])
        foreach path [lsort -index 1 -dictionary $plist] {
            set pathlist [lindex $path 0]
            set path [lindex $pathlist 0]
            set size $psize($path)

            # if size of page would exceed maximum size,
            # write whatever is in buffer now
            if {([string length $currentchunk] + $size) > $fs(pagesize)} {
                _purgeSmallfilesChunk
            }

            # append data to current chunk
            set offset [string length $currentchunk]
            lappend currentchunkPOS $pathlist $offset $size
            append currentchunk $pbuf($path)
        }

        # dump remaining files, if any
        _purgeSmallfilesChunk

        array set fs {
            smallfilepaths {}
            smallfilebuf {}
            smallfilebufsize 0
        }
    }
}

proc cookfs::writeFiles {fsid args} {
    upvar #0 $fsid fs
    # args - path type data size

    # iterate over arguments
    foreach {path datatype data size} $args {
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

        #vfs::log [list cookfs::writeFiles write $path with $size byte(s)]

        # check if this is a small file or writing to memory has been enabled
        if {($size <= $fs(smallfilesize)) || $fs(writetomemory)} {
            # add file to "small file buffer" instead of writing to disk
            if {$rawdata} {
                set fc $data
            }  else  {
                set fc [read $chan]
            }

            set sfidx [llength $fs(smallfilebuf)]
            lappend fs(smallfilebuf) $fc
            lappend fs(smallfilepaths) $path $size $clk
            incr fs(smallfilebufsize) [string length $fc]

            $fs(index) set $path $clk [list [expr {-$sfidx - 1}] 0 $size]
            incr fs(changeCount)

            # if current buffer exceeds maximum, write small files to clean it
            # but only if not writing to memory
            if {(!$fs(writetomemory)) && ($fs(smallfilebufsize) >= $fs(smallfilebuffersize))} {
                purgeSmallfiles $fsid
            }
        }  else  {
            # large file - written as chunks
            set left $size
            set chunklist [list]

            while {$left > 0} {
                if {$left > $fs(pagesize)} {
                    set read $fs(pagesize)
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

                #vfs::log [list cookfs::writeFiles write $path left $left read $read result $datalen]

                if {$datalen == 0} {break}

                set chunkid [$fs(pages) add $data]

                lappend chunklist $chunkid 0 $datalen

                incr left -$datalen
            }

            #vfs::log [list cookfs::writeFiles $path as $chunklist]
            $fs(index) set $path $clk $chunklist
            incr fs(changeCount)
        }
        if {$doclose} {close $chan}
    }
}

package provide vfs::cookfs::tcl::writer 1.3
