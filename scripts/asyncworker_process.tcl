# Helper commands for pags and async pages handling
#
# (c) 2014 Wojciech Kocjan

package require md5 2

namespace eval cookfs {}
namespace eval cookfs::asyncworker {}
namespace eval cookfs::asyncworker::process {}

set cookfs::asyncworker::process::handleIdx 0

# TODO: after first call to cleanup, it is no longer possible to do any compression
proc cookfs::asyncworker::process {args} {
    set name ::cookfs::asyncworker::process[incr ::cookfs::asyncworker::process::handleIdx]
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
        error "Invalid options for cookfs::asyncworker::process"
    }

    if {![string is integer -strict $o(maxqueued)] || ($o(maxqueued) <= 0)} {
        set o(maxqueued) [expr {$o(count) * 2}]
    }

    upvar #0 $name aw
    array set aw [array get o]
    array set $name.available {}

    if {[catch {
	# initialize all child processes
	for {set i 0} {$i < $aw(count)} {incr i} {
	    set fh [open "|$o(commandline)" r+]
	    set helloMessage [format %08x%08x [pid] [pid $fh]]
	    lappend aw(channelsIdle) $fh
	    fconfigure $fh -translation binary -buffering none -blocking 0 -buffersize $aw(buffersize)
	    puts -nonewline $fh $helloMessage
	    puts -nonewline $fh [binary format I $aw(buffersize)]
	    ::cookfs::asyncworker::process::writeData $fh $aw(data)
	    flush $fh
	}
	# wait for hello response and ensure communication
	# with compression process works properly to avoid errors with
	# output from child process keeping compression hanging
	set timeout 10
	set i 0
	foreach fh $aw(channelsIdle) {
	    set helloMessage [format %08x%08x [pid] [pid $fh]]
	    set out {}
	    while {[string length $out] != 16} {
		if {$timeout < 0} {error "Unable to initialize child process(es)"}
		if {[string length $out] != 16} {
		    after 1000
		    incr timeout -1
		}
		append out [read $fh]
	    }
	    if {$out != $helloMessage} {
		error "Unable to initialize child process(es)"
	    }
	    fconfigure $fh -blocking 1
	}
    } err]} {
	set ei $::errorInfo
	set ec $::errorCode
	foreach ch $aw(channelsIdle) {
	    catch {close $ch}
	}
	error "Unable to create child process: $err" $ei $ec
    }

    return [list ::cookfs::asyncworker::process::handle $name]
}

proc ::cookfs::asyncworker::process::finalize {name} {
    upvar #0 $name aw $name.available av
    foreach chan $aw(channelsIdle) {
	catch {close $chan}
    }
    unset $name
    unset $name.available
}

proc ::cookfs::asyncworker::process::handle {name cmd idx arg} {
    upvar #0 $name aw $name.available av
    # detect already closed handles
    if {![info exists aw(open)]} {
        return
    }
    if {$cmd == "process"} {
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

        if {$idx == -1} {
            set idx [lindex [array names av] 0]
        }
        if {[info exists av($idx)]} {
            set contents $av($idx)
            unset av($idx)
            set rc [list $idx $contents]
        }  else  {
            set rc {}
        }
        return $rc
    }  elseif {$cmd == "finalize"} {
	finalize $name
	return true
    }
}

proc cookfs::asyncworker::process::worker {callback} {
    fconfigure stdin -translation binary -buffering none -blocking 1
    fconfigure stdout -translation binary -buffering none -blocking 1

    puts -nonewline stdout [read stdin 16]
    flush stdout

    if {![binary scan [read stdin 4] I buffersize]} {
	exit 1
    }
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

proc cookfs::asyncworker::process::readDataNonBlocking {chan var} {
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

proc cookfs::asyncworker::process::readDataBlocking {chan var} {
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

proc cookfs::asyncworker::process::writeData {chan data} {
    puts -nonewline $chan [binary format I [string length $data]]$data
    flush $chan
}

package provide vfs::cookfs::asyncworker::process 1.4
