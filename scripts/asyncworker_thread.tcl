# Helper commands for pags and async pages handling
#
# (c) 2014 Wojciech Kocjan

package require md5 2

namespace eval cookfs {}
namespace eval cookfs::asyncworker::thread {}

set cookfs::asyncworker::thread::handleIdx 0

# TODO: after first call to cleanup, it is no longer possible to do any compression
proc cookfs::asyncworker::thread {args} {
    package require Thread

    set name ::cookfs::asyncworker::thread[incr ::cookfs::asyncworker::thread::handleIdx]
    array set o {
        count 1
        maxqueued {}
        callback {}
        initscript {}
        threadsIdle {}
        threadsBusy {}
        open true
    }
    foreach {n v} $args {
        switch -- $n {
            -count - -c {
                set o(count) $v
            }
            -callback {
                set o(callback) $v
            }
            -initscript {
                set o(initscript) $v
            }
            default {
                error "Unknown option $n"
            }
        }
    }

    if {($o(count) < 0)} {
        error "Invalid options for cookfs::asyncworker::thread"
    }

    if {![string is integer -strict $o(maxqueued)] || ($o(maxqueued) <= 0)} {
        set o(maxqueued) [expr {$o(count) * 2}]
    }

    upvar #0 $name aw
    array set aw [array get o]
    array set $name.available {}
    set aw(mutex) [thread::mutex create]
    set aw(cond) [thread::cond create]

    set script {}
    append script {
        # needed for cookfs::binarydata
        load {} Cookfs
        namespace eval ::cookfs::asyncworker::thread {}
        proc ::cookfs::asyncworker::thread::process {idx data} {
            variable name
            variable cond
            variable mutex
            variable callback

            if {[catch {
                set data [cookfs::binarydata retrieve $data]
                set rc [uplevel #0 [linsert $callback end $data]]
                set rc [cookfs::binarydata create $rc]
                set ok 1
            } err]} {
                set rc $err
                set ok 0
            }
            if {[catch {
                thread::mutex lock $mutex
                tsv::set $name $idx [list $ok $rc]
                thread::cond notify $cond
                thread::mutex unlock $mutex
            }]} {
                catch {thread::mutex unlock $mutex}
            }
        }
    }
    append script [list set ::cookfs::asyncworker::thread::name $name] \n
    append script [list set ::cookfs::asyncworker::thread::mutex $aw(mutex)] \n
    append script [list set ::cookfs::asyncworker::thread::cond $aw(cond)] \n
    append script [list set ::cookfs::asyncworker::thread::callback [lrange $o(callback) 0 end]] \n
    append script [list set ::initscript $o(initscript)] \n
    # wrap initscript in a catch and return result
    # from initialization - currently ignored
    append script {
        if {[catch $::initscript err]} {
            set ::initscriptresult [list 0 $err]
        } else {
            set ::initscriptresult [list 1]
        }
        unset ::initscript
        set ::initscriptresult
    }
    set aw(script) $script
    set aw(initialized) 0

    return [list ::cookfs::asyncworker::thread::handle $name]
}

proc ::cookfs::asyncworker::thread::init {name} {
    upvar #0 $name aw
    if {!$aw(initialized)} {
        set aw(threadsIdle) {}
        for {set i 0} {$i < $aw(count)} {incr i} {
            set tid [thread::create -joinable]
            thread::send -async $tid "[list namespace eval ::cookfs::asyncworker::thread [list set tid $tid]]\n$aw(script)" ${name}(initresult-$tid)
            lappend aw(threadsIdle) $tid
        }
        set aw(initialized) 1
    }
}

proc ::cookfs::asyncworker::thread::free {name} {
    upvar #0 $name aw
    if {$aw(initialized)} {
        # wait for any pending threads
        while {[llength $aw(threadsBusy)] > 0} {
            catch {handle $name wait -1 1} kk
        }
        foreach tid $aw(threadsIdle) {
            thread::send -async $tid {thread::exit}
            thread::join $tid
        }
        set aw(threadsIdle) {}
        set aw(initialized) 0
    }
}

proc ::cookfs::asyncworker::thread::finalize {name} {
    upvar #0 $name aw $name.available av
    thread::mutex destroy $aw(mutex)
    thread::cond destroy $aw(cond)
    # this may fail if no thread was ever run
    catch {tsv::unset $name}
    unset $name
    unset $name.available
}

proc ::cookfs::asyncworker::thread::handle {name cmd idx arg} {
    upvar #0 $name aw $name.available av

    # detect already closed handles
    if {![info exists aw(open)]} {
        return
    }
    if {$cmd == "process"} {
        init $name
        set tid [lindex $aw(threadsIdle) 0]
        set aw(threadsIdle) [lrange $aw(threadsIdle) 1 end]
        lappend aw(threadsBusy) $idx $tid
        set arg [cookfs::binarydata create $arg]
        thread::send -async $tid [list ::cookfs::asyncworker::thread::process $idx $arg]
    }  elseif {$cmd == "wait"} {
        init $name
        while {[llength $aw(threadsBusy)] > 0} {
            set busy {}
            set ok 1
            set rc {}

            thread::mutex lock $aw(mutex)
            if {[catch {
                foreach {i tid} $aw(threadsBusy) {
                    if {[tsv::exists $name $i]} {
                        set rc [tsv::get $name $i]
                        tsv::unset $name $i

                        set ok [lindex $rc 0]
                        if {[catch {
                            set rc [lindex $rc 1]
                            set rc [cookfs::binarydata retrieve $rc]
                        }]} {
                            set ok 0
                        }

                        lappend aw(threadsIdle) $tid
                        if {$ok} {
                            set av($i) $rc
                        }
                    }  else  {
                        lappend busy $i $tid
                    }
                }
                set aw(threadsBusy) $busy
                if {!$ok} {
                    # exit if retrieval failed
                    set wait 0
                }  elseif {($arg || ([llength [array names av]] >= $aw(maxqueued))) && ([llength $aw(threadsBusy)] == 0)} {
                    # if this is the end, exit if all channels are freed
                    set wait 0
                }  elseif {($idx < 0 || [info exists av($idx)]) && ([llength $aw(threadsIdle)] > 0)} {
                    # exit if current chunk was retrieved or any chunk was requested
                    set wait 0
                }  else  {
                    # wait otherwise
                    set wait 1
                }

                # if we should wait, sleep up to 0.1s or when a thread notifies us
                if {$wait} {
                    thread::cond wait $aw(cond) $aw(mutex) 100
                }
            } err]} {
                thread::mutex unlock $aw(mutex)
                error $err
            }
            thread::mutex unlock $aw(mutex)

            if {!$ok} {
                error "Unable to retrieve chunk: $rc"
            }  elseif {!$wait} {
                break
            }
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
    }  elseif {$cmd == "free"} {
        free $name
        return true
    }  elseif {$cmd == "finalize"} {
        free $name
        finalize $name
        return true
    }
}

package provide cookfs::asyncworker::thread 1.9.0
