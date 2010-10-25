# Tcl part of Cookfs package
#
# (c) 2010 Wojciech Kocjan, Pawel Salawa

namespace eval cookfs {}
namespace eval vfs::cookfs {}

# load required packages
package require vfs

if {![info exists cookfs::mountId]} {
    set cookfs::mountId 0
}

proc vfs::cookfs::Mount {args} {
    return [eval [concat [list ::cookfs::Mount] $args]]
}

# initialize package - load Tcl or C versions of parts of cookfs
proc cookfs::initialize {} {
    variable pkginitialized

    if {![info exists pkginitialized]} {
	package require vfs::cookfs::pkgconfig

	# load Tcl versions of packages for now
	package require vfs::cookfs::c [pkgconfig get package-version]
	package require vfs::cookfs::tcl::vfs [pkgconfig get package-version]
	package require vfs::cookfs::tcl::readerchannel [pkgconfig get package-version]
	package require vfs::cookfs::tcl::memchan [pkgconfig get package-version]
	package require vfs::cookfs::tcl::writer [pkgconfig get package-version]
	
	if {![pkgconfig get c-pages]} {
	    package require vfs::cookfs::tcl::pages [pkgconfig get package-version]
	}

	if {![pkgconfig get c-fsindex]} {
	    package require vfs::cookfs::tcl::fsindex [pkgconfig get package-version]
	}

	set pkginitialized 1
    }
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
        {endoffset.arg                  ""              {Force reading VFS ending at specific offset instead of end of file}}
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

    set showhelp 0

    set newargs {}
    set help "Usage: vfs::cookfs::Mount ?options? archive local ?options?\nOptions are:"
    foreach o [lsort -index 0 $options] {
	set name [lindex $o 0]
	if {[regexp "^(.*)\\.arg\$" $name - name]} {
	    set opt($name) [lindex $o 1]
	    set optarg(-$name) 1
	    set vhelp " (default: [lindex $o 1])"
	}  else  {
	    set opt($name) 0
	    set optarg(-$name) 0
	    set vhelp " (flag)"
	}
	append help [format "\n %-20s %s%s" "-$name" [lindex $o end] $vhelp]
    }

    set setopt {}
    foreach arg $args {
	if {$setopt != ""} {
	    set opt($setopt) $arg
	    set setopt ""
	}  elseif {[info exists optarg($arg)]} {
	    if {$optarg($arg)} {
		set setopt [string range $arg 1 end]
	    }  else  {
		set opt([string range $arg 1 end]) 1
	    }
	}  elseif {$arg == "-help"} {
	    set showhelp 1
	    break
	}  else  {
	    lappend newargs $arg
	}
    }
    if {($setopt != "") || ([llength $newargs] != 2)} {
	set showhelp 1
    }
    unset optarg

    if {$showhelp} {
	error $help
    }

    # extract paths from remaining arguments
    set archive [lindex $newargs 0]
    set local [lindex $newargs 1]

    # if write to memory option was selected, open archive as read only anyway
    if {$opt(writetomemory)} {
        set opt(readonly) 1
    }

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
        channelId 0
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

        if {$opt(endoffset) != ""} {
            lappend pagesoptions -endoffset $opt(endoffset)
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

cookfs::initialize

package provide vfs::cookfs 1.1
