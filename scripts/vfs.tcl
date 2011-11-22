# Handling for Tcl VFS functionality.
#
# (c) 2010 Wojciech Kocjan, Pawel Salawa

namespace eval cookfs {}

# error codes used in CookFS (used for bootstraping CooKit)
set cookfs::posix(ENOENT) 2
set cookfs::posix(ENOTDIR) 20
set cookfs::posix(EISDIR) 21
set cookfs::posix(EROFS) 30

# dispatcher for commands
proc cookfs::vfshandler {fsid cmd root relative actualpath args} {
    upvar #0 $fsid fs

    #vfs::log [list cookfs::vfshandler $fsid $cmd $relative $args]

    set rc [list]

    # TODO: rewrite everything to separate commands
    switch -- $cmd {
        createdirectory {
            # create directory
            if {[catch {
                vfshandleCreatedirectory $fsid $root $relative $actualpath
            } error]} {
                #vfs::log [list cookfs::vfshandler createdirectory $fsid $relative $error]
                vfs::filesystem posixerror $::cookfs::posix(ENOTDIR)
            }
        }
        deletefile {
            incr fs(changeCount)
            set rc [vfshandleDelete $fsid $root $relative $actualpath file false]
        }
        removedirectory {
            incr fs(changeCount)
            set rc [vfshandleDelete $fsid $root $relative $actualpath directory [lindex $args 0]]
        }
        fileattributes {
            set rc [vfshandleFileattributes \
                $fsid $root $relative $actualpath $args]
        }
        matchindirectory {
            set rc [vfshandleMatchindirectory $fsid \
                $relative $actualpath [lindex $args 0] [lindex $args 1]]
        }
        stat {
            set rc [vfshandleStat $fsid $relative $actualpath]
        }
        open {
            # we skip permissions as we don't really have on
            set rc [vfshandleOpen $fsid $relative [lindex $args 0]]
        }
        access {
            vfshandleStat $fsid $relative $actualpath
            set rc true
        }
        utime {
            incr fs(changeCount)

            # modify mtime and atime, assuming file exists
            if {[catch {vfshandleStat $fsid $relative $actualpath} rc]} {
                vfs::filesystem posixerror $::cookfs::posix(ENOENT)
            }

            $fs(index) setmtime $relative [lindex $args 1]
        }
    }
    return $rc
}

proc cookfs::vfshandleCreatedirectory {fsid root relative actualpath} {
    upvar #0 $fsid fs

    if {$fs(nodirectorymtime)} {
        set clk 0
    }  else  {
        set clk [clock seconds]
    }
    $fs(index) set $relative $clk
    incr fs(changeCount)
}

# handle file attributes
proc cookfs::vfshandleFileattributes {fsid root relative actualpath a} {
    upvar #0 $fsid fs
    switch -- [llength $a] {
        0 {
            # list strings
            return [::vfs::listAttributes]
        }
        1 {
            # get value
            set index [lindex $a 0]
            return [::vfs::attributesGet $root $relative $index]

        }
        2 {
            # set value
            incr fs(changeCount)
            if {0} {
                # handle read-only
                vfs::filesystem posixerror $::cookfs::posix(EROFS)
            }
            set index [lindex $a 0]
            set val [lindex $a 1]
            return [::vfs::attributesSet $root $relative $index $val]
        }
    }
}

# implementation of stat
proc cookfs::vfshandleStat {fsid relative actualpath} {
    upvar #0 $fsid fs
    upvar #0 $fsid.dir fsdir

    array set rc {
        dev 0
        ino 0
        mode 7
        nlink 1
        uid 0
        gid 0
        atime 0
        mtime 0
        ctime 0
    }

    #vfs::log [list cookfs::vfshandleStat $fsid $relative]

    if {$relative == ""} {
        # for trying to stat the actual archive, we just return dummy values
        array set rc {
            type directory size 0 atime 0 mtime 0 ctime 0
        }
    }  elseif {[catch {
        set fileinfo [$fs(index) get $relative]
    }]} {
        # if trying to get index entry failed, the file does not exist
        #vfs::log [list cookfs::vfshandleStat $fsid $relative {unable to get index entry}]
        vfs::filesystem posixerror $::cookfs::posix(ENOENT)
    }  else  {
        if {[llength $fileinfo] == 3} {
            set rc(type) file
            set rc(size) [lindex $fileinfo 1]
        }  else  {
            set rc(size) 0
            set rc(type) directory
        }
        set rc(mtime) [lindex $fileinfo 0]
        set rc(atime) $rc(mtime)
        set rc(ctime) $rc(mtime)

        #vfs::log [list cookfs::vfshandleStat $fsid success [array get rc]]
    }

    return [array get rc]
}

# matching of entries within directories
proc cookfs::vfshandleMatchindirectory {fsid relative actualpath pattern types} {
    upvar #0 $fsid fs

    if {$types == 0} {
        set lengthlist {1 3}
    }  else  {
        set lengthlist [list]
        if {($types & 4) !=0} {
            lappend lengthlist 1
        }
        if {($types & 16) !=0} {
            lappend lengthlist 3
        }
    }
    if {![string length $pattern]} {
        set result [list]
        if {![catch {$fs(index) get $relative} fileinfo]} {
            if {[lsearch -exact $lengthlist [llength $fileinfo]] >= 0} {
                set result [list $actualpath]
            }
        }
    }  else  {
        if {[catch {
            set result [$fs(index) list $relative]
        }]} {
	    set result [list]
	}  else  {
            #vfs::log [list cookfs::vfshandleMatchindirectory $fsid list $relative $result]
            set res $result
	    set result [list]
            foreach res $res {
                if {![catch {$fs(index) get [file join $relative $res]} fileinfo]} {
                    if {[lsearch -exact $lengthlist [llength $fileinfo]] >= 0} {
                        if {[string match $pattern $res]} {
                            lappend result [file join $actualpath $res]
                        }
                    }
                }
            }
            #vfs::log [list cookfs::vfshandleMatchindirectory $fsid $relative $pattern matches $result]
        }
    }
    return $result
}

# delete a file or directory, internal implementation of deletefile and removedirectory
proc cookfs::vfshandleDelete {fsid root relative actualpath type recursive} {
    upvar #0 $fsid fs
    #vfs::log [list cookfs::vfshandleDelete $fsid $relative $type $recursive]
    if {[catch {vfshandleStat $fsid $relative $actualpath} rc]} {
        vfs::filesystem posixerror $::cookfs::posix(ENOENT)
    }

    array set a $rc
    if {$a(type) != $type} {
        if {$type == "file"} {
            vfs::filesystem posixerror $::cookfs::posix(EISDIR)
        }  else  {
            vfs::filesystem posixerror $::cookfs::posix(ENOTDIR)
        }
    }  elseif {$type == "directory" && $recursive} {
        if {[catch {
            foreach ch [$fs(index) list $relative] {
                # check type and delete appropriately
                set g [$fs(index) get [file join $relative $ch]]
                if {[llength $g] == 1} {
                    vfshandleDelete $fsid $root [file join $relative $ch] [file join $actualpath $ch] "directory" 1
                }  else  {
                    vfshandleDelete $fsid $root [file join $relative $ch] [file join $actualpath $ch] "file" 0
                }
            }
        } err]} {
            #vfs::log [list cookfs::vfshandleDelete $fsid $relative $type $recursive error $err]
            vfs::filesystem posixerror $::cookfs::posix(ENOENT)
        }
    }

    $fs(index) unset $relative
}

# open a file for reading or writing
proc cookfs::vfshandleOpen {fsid relative mode} {
    upvar #0 $fsid fs

    set dir [file dirname $relative]

    if {($dir != "") && ($dir != ".")} {
        if {[catch {
            set g [$fs(index) get $dir]
        }]} {
            vfs::filesystem posixerror $::cookfs::posix(ENOENT)
        }  elseif  {[llength $g] != 1} {
            vfs::filesystem posixerror $::cookfs::posix(ENOTDIR)
        }
    }

    switch -- $mode {
        "" - r {
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative read]
            if {[catch {
                set channel [cookfs::createReadableChannel $fsid $relative]
            } error]} {
                #vfs::log [list cookfs::vfshandleOpen $fsid $relative error $::errorInfo]
                vfs::filesystem posixerror $::cookfs::posix(ENOENT)
            }

            if {$channel == ""} {
                #vfs::log [list cookfs::vfshandleOpen $fsid $relative {does not exist}]
                vfs::filesystem posixerror $::cookfs::posix(ENOENT)
            }

            return [list $channel ""]
        }
        r+ {
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative read+]

            set rc [cookfs::initMemchan $fsid $relative true]
            
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative result $rc]
            return $rc
        }
        w - w+ {
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative write]

            set rc [cookfs::initMemchan $fsid $relative false]
            
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative result $rc]
            return $rc
        }
        a - a+ {
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative append]
            if {[catch {vfshandleStat $fsid $relative $actualpath} rc]} {
                vfs::filesystem posixerror $::cookfs::posix(ENOENT)
            }

            set rc [cookfs::initMemchan $fsid $relative true]

            seek [lindex $rc 0] 0 end
            
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative result $rc]
            return $rc
        }
        default {
            error "Unknown mode $mode"
        }
    }
}

package provide vfs::cookfs::tcl::vfs 1.3.1
