# Tcl part of Cookfs package
#
# (c) 2010 Pawel Salawa

namespace eval cookfs {}
namespace eval vfs::cookfs {}

# load required packages
package require cmdline
package require vfs

if {![info exists cookfs::mountId]} {
    set cookfs::mountId 0
}

proc vfs::cookfs::Mount {args} {
    return [::cookfs::Mount {*}$args]
}

# Mount VFS - usage:
# cookfs::Mount ?options? archive local
# Run cookfs::Mount -help for a description of all options
#
proc cookfs::Mount {args} {
    variable mountId

    #
    # Parse options and get paths from args variable
    #
    set options {
        {noregister                                     {Do not register this filesystem (for temporary writes)}}
        {bootstrap.arg                  ""              {Bootstrap code to add in the beginning}}
        {compression.arg                "zlib"          {Compression type to use}}
        {pagesobject.arg                ""              {Reuse existing pages object}}
        {readonly                                       {Open as read only}}
        {writetomemory                                  {Open as read only and keep new files in memory}}
        {pagesize.arg                   262144          {Maximum page size}}
        {pagecachesize.arg              8               {Number of pages to cache}}
        {volume                                         {Mount as volume}}
        {smallfilesize.arg              32768           {Maximum size of small files}}
        {smallfilebuffer.arg            4194304         {Maximum buffer for optimizing small files}}
    }
    lappend options [list tempfilename.arg "" {Temporary file name to keep index in}]

    set usage {archive local}

    if {[catch {
        array set opt [cmdline::getoptions args $options $usage]
    } error]} {
        return -code error -errorinfo $error $error
    }

    if {[llength $args] != 2} {
        set args -help
        catch {cmdline::getoptions args $options $usage} error
        return -code error -errorinfo $error $error
    }

    # if write to memory option was selected, open archive as read only anyway
    if {$opt(writetomemory)} {
        set opt(readonly) 1
    }

    #
    # extract paths from remaining arguments
    #
    set archive [lindex $args 0]
    set local [lindex $args 1]

    if {![catch {package require Tcl 8.4}]} {
        set archive [file normalize [file join [pwd] $archive]]
        set local [file normalize [file join [pwd] $local]]
    }

    if {$opt(readonly) && (!$opt(writetomemory)) && (![file exists $archive])} {
        set e "File \"$archive\" does not exist"
        return -code error -errorinfo $e $e
    }

    set fsid ::cookfs::mount[incr mountId]

    # TODO: cleanup on error
    upvar #0 $fsid fs

    # initialize FS data
    array set fs {
        channelid 0
        smallfilepaths {}
        smallfilebuf {}
        smallfilebufsize 0
    }
    set fs(archive) $archive
    set fs(local) $local
    set fs(pagesize) $opt(pagesize)
    set fs(noregister) $opt(noregister)
    set fs(smallfilesize) $opt(smallfilesize)
    set fs(smallfilebuffersize) $opt(smallfilebuffer)
    set fs(changeCount) 0
    set fs(writetomemory) $opt(writetomemory)

    # initialize pages
    if {$opt(pagesobject) == ""} {
        set pagesoptions [list -compression $opt(compression) -cachesize $opt(pagecachesize)]
        if {$opt(readonly)} {
            lappend pagesoptions -readonly
        }
        set pagescmd [concat [list ::cookfs::pages] $pagesoptions [list $archive]]
        
        set fs(pages) [eval $pagescmd]
    }  else  {
        set fs(pages) $opt(pagesobject)
    }
    
    if {([$fs(pages) length] == 0) && ([string length $opt(bootstrap)] > 0)} {
        $fs(pages) add $opt(bootstrap)
    }

    # initialize directory listing
    set idx [$fs(pages) index]
    if {[string length $idx] > 0} {
        set fs(index) [cookfs::fsindex $idx]
    }  else  {
        set fs(index) [cookfs::fsindex]
    }

    # initialize Tcl mountpoint
    set mountcommand [list vfs::filesystem mount]
    if {$opt(volume)} { lappend mountcommand -volume }

    lappend mountcommand $local [list cookfs::vfshandler $fsid]
    if {!$fs(noregister)} {
        eval $mountcommand
        set unmountcmd [list ::cookfs::Unmount $fsid]
        if {[info commands ::vfs::RegisterMount] != ""} {
            vfs::RegisterMount $local $unmountcmd
        }  else  {
            set ::vfs::_unmountCmd([file normalize $local]) $unmountcmd
        }
    }

    return $fsid
}

proc cookfs::Unmount {fsid args} {
    upvar #0 $fsid fs

    # finalize any small files pending write
    purgeSmallfiles $fsid

    if {$fs(changeCount) > 0 && (!$fs(writetomemory))} {
        $fs(pages) index [$fs(index) export]
    }

    # finalize pages and index
    $fs(pages) delete
    $fs(index) delete

    # unmount filesystem
    if {!$fs(noregister)} {
        vfs::filesystem unmount $fs(local)
    }
    unset $fsid
}

proc cookfs::aside {fsid filename} {
    upvar #0 $fsid fs
    if {$fs(writetomemory)} {
        error "Write to memory option enabled; not creating add-aside archive"
    }

    $fs(pages) aside $filename

    set newindex [cookfs::fsindex [$fs(pages) index]]
    $fs(index) delete
    set fs(index) $newindex
}

proc cookfs::writetomemory {fsid} {
    upvar #0 $fsid fs
    set fs(writetomemory) 1
}

package provide vfs::cookfs 1.0
