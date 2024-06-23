# cookfs::tcl::memchan --
#
# Memchan handling for both in-memory channels and channels that are writable.
#
# (c) 2010 Wojciech Kocjan, Pawel Salawa
# (c) 2011-2014 Wojciech Kocjan
# (c) 2024 Konstantin Kushnir

namespace eval cookfs {}
namespace eval cookfs::tcl {}
namespace eval cookfs::tcl::writerchannel {}

package require vfs

# initialize a memchan, optionally read current contents of the file
proc cookfs::tcl::writerchannel {fsid path read} {
    upvar #0 $fsid fs
    upvar #0 $fsid.dir fsdir
    upvar #0 $fsid.chk fschk

    set chan [vfs::memchan]
    set translation [fconfigure $chan -translation]
    set encoding    [fconfigure $chan -encoding]
    # larger buffer size speeds up memchan
    fconfigure $chan -translation binary -buffersize 262144

    if {$read} {
        if {![catch {
            set fileinfo [$fs(index) get $path]
        }]} {
            foreach {mtime size chunklist} $fileinfo break
            if {([llength $chunklist] == 3) && ([lindex $chunklist 0] < 0)} {
                # It is a small file that's still kept in memory
                puts -nonewline $chan [$fs(writer) getbuf [lindex $chunklist 0]]
                flush $chan
            }  else  {
                # read all chunks to memchan
                foreach {chunkId chunkOffset chunkSize} $chunklist {
                    set d [$fs(pages) get $chunkId]
                    if {($chunkOffset != 0) || ($chunkSize != [string length $d])} {
                        set d [string range $d $chunkOffset [expr {$chunkOffset + $chunkSize - 1}]]
                    }
                    puts -nonewline $chan $d
                }
            }
        }
    }

    # re-seek to start of file, revert to original translation
    seek $chan 0 start
    fconfigure $chan -translation $translation -encoding $encoding

    # return channel along with procedure to invoke after closing it
    return [list $chan \
        [list ::cookfs::tcl::writerchannel::onClose $fsid $path $chan]]
}

# clean up memchan and write its contents to archive
proc cookfs::tcl::writerchannel::onClose {fsid path chan} {
    upvar #0 $fsid fs
    fconfigure $chan -translation binary
    seek $chan 0 end
    set size [tell $chan]
    seek $chan 0 start
    $fs(writer) write $path channel $chan ""
}

package provide cookfs::tcl::writerchannel 1.8.0
