# Handling of read-only channels.
#
# (c) 2010 Pawel Salawa

namespace eval cookfs {}

# creates a new channel for reading data from a VFS
# uses reflected channels
proc cookfs::createReadableChannel {fsid path} {
    variable dirlistParameters
    upvar #0 $fsid fs
    upvar #0 $fsid.dir fsdir
    upvar #0 $fsid.chk fschk

    # try to get information about specified path    
    if {[catch {
        set fileinfo [$fs(index) get $path]
    }]} {
        return ""
    }

    # return if trying to open a directory
    if {([llength $fileinfo] != 3)} {
        return ""
    }

    foreach {mtime size chunklist} $fileinfo break

    set id [incr fs(channelId)]
    set chid "$fsid.ch$id"
    upvar #0 $chid ch

    array set ch {
        offset 0
    }


    # if this is a small file, currently pending write, pass it to memchan
    if {([llength $chunklist] == 3) && ([lindex $chunklist 0] < 0)} {
        #vfs::log [list cookfs::createReadableChannel $fsid $path smallfile]
        return [lindex [initMemchan $fsid $path true] 0]
    }

    # initialize internal information with chunk list
    set ch(chunkinfo) [list]
    set offset 0
    foreach {chunkId chunkOffset chunkSize} $chunklist {
        lappend ch(chunkinfo) $offset $chunkId $chunkOffset $chunkSize
        incr offset $chunkSize
    }

    # create channel
    set ch(refchannel) \
        [chan create {read} [list cookfs::readableChannelHandler $fsid $chid]]

    return $ch(refchannel)
}

# handle command for a channel
proc cookfs::readableChannelHandler {fsid chid command args} {
    upvar #0 $fsid fs
    upvar #0 $chid ch

    switch -- $command {
        initialize {
            return [list initialize finalize watch read seek]
        }
        finalize {
            unset $chid
        }
        watch {
            # TODO: handle readable events
            error "Sorry, not supported yet"
        }

        read {
            # read data from a channel
            foreach {channelId bytesleft} $args break
            set rc ""
            set currentoffset $ch(offset)

            # iterate over chunks and read part of the data that matches our current offset
            foreach {offset chunkId chunkOffset chunkSize} $ch(chunkinfo) {
                if {($offset <= $currentoffset) && (($offset + $chunkSize) > $currentoffset)} {
                    set doffset [expr {$currentoffset - $offset}]
                    set cpoffset [expr {$chunkOffset + $doffset}]
                    set dsize [expr {$chunkSize - $doffset}]
                    if {$dsize > $bytesleft} {
                        set dsize $bytesleft
                    }
                    if {$chunkId < 0} {
                        error "Unable to handle small files not written yet!"
                    }

                    set d [$fs(pages) get $chunkId]
                    if {($cpoffset != 0) || ($dsize != [string length $d])} {
                        set d [string range $d $cpoffset [expr {$cpoffset + $dsize - 1}]]
                    }
                    append rc $d
                    incr bytesleft -[string length $d]
                    incr currentoffset [string length $d]
                }
            }
            set ch(offset) $currentoffset
            return $rc
            
        }
        seek {
            # seek within the file
            foreach {channelId offset base} $args break
            switch -- $base {
                start {
                    set ch(offset) $offset
                }
                current {
                    incr ch(offset) $offset
                }
                end {
                    set ch(offset) [expr {$ch(filesize) - $offset}]
                }
            }
            if {$ch(offset) < 0} {set ch(offset) 0}
            if {$ch(offset) > $ch(filesize)} {set ch(offset) $ch(filesize)}
            return $ch(offset)
        }
        default {
            error "Command not implemented"
        }
    }
}

package provide cookfs 1.0
