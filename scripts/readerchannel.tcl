# Handling of read-only channels.
#
# (c) 2010 Wojciech Kocjan, Pawel Salawa
# (c) 2011-2014 Wojciech Kocjan
# (c) 2024 Konstantin Kushnir

namespace eval cookfs {}
namespace eval cookfs::tcl {}
namespace eval cookfs::tcl::readerchannel {}

# creates a new channel for reading data from a VFS
# uses reflected channels
proc cookfs::tcl::readerchannel {fsid chunklist} {
    variable dirlistParameters
    upvar #0 $fsid fs

    set id [incr fs(channelId)]
    set chid "$fsid.ch$id"
    upvar #0 $chid ch

    array set ch {
        offset 0
        firstread 1
    }

    # initialize internal information with chunk list
    set ch(chunkinfo) [list]
    set offset 0
    foreach {chunkId chunkOffset chunkSize} $chunklist {
        lappend ch(chunkinfo) $offset $chunkId $chunkOffset $chunkSize
        incr offset $chunkSize
    }
    set ch(filesize) $offset

    # create Tcl channel
    if {[info commands ::chan] != ""} {
	set ch(refchannel) \
	    [::chan create {read} [list cookfs::tcl::readerchannel::handler $fsid $chid]]
    }  else  {
	# fallback to rechan
	set ch(refchannel) \
	    [rechan [list cookfs::tcl::readerchannel::handler $fsid $chid] 2]
    }

    fconfigure $ch(refchannel) -buffersize 65536
    return $ch(refchannel)
}

# event handling for watch subcommand
proc cookfs::tcl::readerchannel::eventClean {fd} {
    variable eventEnable
    eventSet $fd 0
}

proc cookfs::tcl::readerchannel::eventWatch {fd a} {
    if {[lindex $a 0] == "read"} {
	eventSet $fd 1
    }  else  {
	eventSet $fd 0
    }
}

proc cookfs::tcl::readerchannel::eventSet {fd e} {
    variable eventEnable
    set cmd [list ::cookfs::tcl::readerchannel::eventPost $fd]
    after cancel $cmd
    if {$e} {
	set eventEnable($fd) 1
	after 0 $cmd
    }  else  {
	catch {unset eventEnable($fd)}
    }
}

proc cookfs::tcl::readerchannel::eventPost {fd} {
    variable eventEnable
    if {[info exists eventEnable($fd)] && $eventEnable($fd)} {
        chan postevent $fd read
    }
}

# handle command for a channel
proc cookfs::tcl::readerchannel::handler {fsid chid command args} {
    upvar #0 $fsid fs
    upvar #0 $chid ch

    switch -- $command {
        initialize {
            return [list initialize finalize watch read seek]
        }
        finalize - close {
            eventClean [lindex $args 0]
            unset $chid
        }
        watch {
            eventWatch [lindex $args 0] [lindex $args 1]
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

                    if { $ch(firstread) } {
                        if { ![$fs(pages) getcache $chunkId] } {
                            $fs(pages) ticktock
                        }
                        set ch(firstread) 0
                    }
                    set weight [expr { [$fs(index) getblockusage $chunkId] > 1 ? 1 : 0 }]
                    set d [$fs(pages) get -weight $weight $chunkId]
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
                start - 0 {
                    set ch(offset) $offset
                }
                current - 1 {
                    incr ch(offset) $offset
                }
                end - 2 {
                    set ch(offset) [expr {$ch(filesize) + $offset}]
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

package provide cookfs::tcl::readerchannel 1.9.0
