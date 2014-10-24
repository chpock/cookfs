# Helper commands for pags and async pages handling
#
# (c) 2014 Wojciech Kocjan

package require md5 2

namespace eval cookfs {}
namespace eval cookfs::asyncworker {}

set cookfs::asyncworker::handleIdx 0

# TODO: after first call to cleanup, it is no longer possible to do any compression
proc cookfs::asyncworker {args} {
    set name ::cookfs::asyncworker[incr ::cookfs::asyncworker::handleIdx]
    array set o {
	buffersize 5242880
        count 1
        maxqueued {}
        commandline {}
        data {}
        channelsIdle {}
        channelsBusy {}
        open true
    }
    foreach {n v} $args {
        switch -- $n {
            -buffersize - -buf - -bs {
		set o(buffersize) $v
	    }
            -count - -c {
                set o(count) $v
            }
            -commandline - -command - -cmd {
                set o(commandline) $v
            }
            -data - -d {
                set o(data) $v
            }
            default {
                error "Unknown option $n"
            }
        }
    }

    if {($o(commandline) == {}) || ($o(count) < 0)} {
        error "Invalid options for cookfs::asyncworker"
    }

    if {![string is integer -strict $o(maxqueued)] || ($o(maxqueued) <= 0)} {
        set o(maxqueued) [expr {$o(count) * 2}]
    }

    upvar #0 $name aw
    array set aw [array get o]
    array set $name.available {}

    for {set i 0} {$i < $aw(count)} {incr i} {
        set fh [open "|$o(commandline)" r+]
        fconfigure $fh -translation binary -buffering none -blocking 1 -buffersize $aw(buffersize)
        puts -nonewline $fh [binary format I $aw(buffersize)]
        ::cookfs::asyncworker::writeData $fh $aw(data)
        lappend aw(channelsIdle) $fh
    }

    return [list ::cookfs::asyncworker::handle $name]
}

proc ::cookfs::asyncworker::handle {name cmd idx arg} {
    upvar #0 $name aw $name.available av
    # detect already closed handles
    if {![info exists aw(open)]} {
        return
    }
    if {$cmd == "compress"} {
        set chan [lindex $aw(channelsIdle) 0]
        set aw(channelsIdle) [lrange $aw(channelsIdle) 1 end]
        lappend aw(channelsBusy) $idx $chan
        writeData $chan $arg
    }  elseif {$cmd == "wait"} {
        while {[llength $aw(channelsBusy)] > 0} {
            set busy {}
            foreach {i fh} $aw(channelsBusy) {
                if {[readDataNonBlocking $fh contents]} {
                    set av($i) $contents
                    lappend aw(channelsIdle) $fh
                }  else  {
                    lappend busy $i $fh
                }
            }
            set aw(channelsBusy) $busy
            # if this is the end, exit if all channels are freed
            if {($arg || ([llength [array names av]] >= $aw(maxqueued))) && ([llength $aw(channelsBusy)] == 0)} {
                break
            }  elseif {([llength $aw(channelsIdle)] > 0)} {
                break
            }
            # TODO: move to events?
            after 50
        }

        if {[info exists av($idx)]} {
            set contents $av($idx)
            unset av($idx)
            set rc [list $idx $contents]
        }  else  {
            set rc {}
        }
        if {$arg && ([llength $aw(channelsBusy)] == 0) && ([llength [array names av]] == 0)} {
            foreach chan $aw(channelsIdle) {
                catch {close $chan}
            }
            unset $name
            unset $name.available
        }
        return $rc
    }
}

proc cookfs::asyncworker::worker {callback} {
    fconfigure stdin -translation binary -buffering none -blocking 1
    fconfigure stdout -translation binary -buffering none -blocking 1
    binary scan [read stdin 4] I buffersize
    fconfigure stdin -buffersize $buffersize
    fconfigure stdout -buffersize $buffersize

    readDataBlocking stdin data
    while {![eof stdin]} {
        if {[readDataBlocking stdin contents]} {
            set contents [uplevel #0 [concat $callback [list $data $contents]]]
            writeData stdout $contents
        }
    }
    exit 0
}

proc cookfs::asyncworker::readDataNonBlocking {chan var} {
    upvar $var v
    set v {}
    fconfigure $chan -blocking 0
    set b [read $chan 1]
    if {[string length $b] != 0} {
        fconfigure $chan -blocking 1
        append b [read $chan 3]
        binary scan $b I len
        set v [read $chan $len]
        return true
    } else {
        return false
    }
}

proc cookfs::asyncworker::readDataBlocking {chan var} {
    upvar $var v
    set v {}
    set len -1
    fconfigure $chan -blocking 1
    binary scan [read $chan 4] I len
    if {$len < 0} {
        return false
    }
    set v [read $chan $len]
    return true
}

proc cookfs::asyncworker::writeData {chan data} {
    puts -nonewline $chan [binary format I [string length $data]]$data
    flush $chan
}

package provide vfs::cookfs::asyncworker 1.4
