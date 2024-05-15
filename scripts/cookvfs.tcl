# Tcl part of Cookfs package
#
# (c) 2010 Wojciech Kocjan, Pawel Salawa
# (c) 2011-2014 Wojciech Kocjan
# (c) 2024 Konstantin Kushnir

namespace eval cookfs {}
namespace eval vfs::cookfs {}

# load required packages
package require vfs

if {![info exists cookfs::mountId]} {
    set cookfs::mountId 0
}

# by default ignore call to cookfs::debug
proc cookfs::debug {args} {}

proc cookfs::md5 {args} {
    if { ![llength [info commands ::cookfs::c::md5]] } {
	return -code error "cookfs::md5 is not supported in this package"
    }
    tailcall ::cookfs::c::md5 {*}$args
}

proc cookfs::sha256 {args} {
    if { ![llength [info commands ::cookfs::c::sha256]] } {
	return -code error "cookfs::sha256 is not supported in this package"
    }
    tailcall ::cookfs::c::sha256 {*}$args
}

proc cookfs::sha1 {args} {
    if { ![llength [info commands ::cookfs::c::sha1]] } {
	return -code error "cookfs::sha1 is not supported in this package"
    }
    tailcall ::cookfs::c::sha1 {*}$args
}

proc cookfs::pages {args} {
    if {[pkgconfig get c-pages]} {
	if {![llength [info commands ::cookfs::c::pages]]} {
	    package require vfs::cookfs::c::pages [pkgconfig get package-version]
	}
	return [uplevel #0 [concat [list ::cookfs::c::pages] $args]]
    }  else  {
	if {![llength [info commands ::cookfs::tcl::pages]]} {
	    package require vfs::cookfs::tcl::pages [pkgconfig get package-version]
	}
	return [uplevel #0 [concat [list ::cookfs::tcl::pages] $args]]
    }
}

proc cookfs::fsindex {args} {
    if {[pkgconfig get c-fsindex]} {
	if {![llength [info commands ::cookfs::c::fsindex]]} {
	    package require vfs::cookfs::c::fsindex [pkgconfig get package-version]
	}
	return [uplevel #0 [concat [list ::cookfs::c::fsindex] $args]]
    }  else  {
	if {![llength [info commands ::cookfs::tcl::fsindex]]} {
	    package require vfs::cookfs::tcl::fsindex [pkgconfig get package-version]
	}
	return [uplevel #0 [concat [list ::cookfs::tcl::fsindex] $args]]
    }
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
	package require vfs::cookfs::tcl::vfs [pkgconfig get package-version]
	package require vfs::cookfs::tcl::memchan [pkgconfig get package-version]
	package require vfs::cookfs::tcl::writer [pkgconfig get package-version]
	package require vfs::cookfs::tcl::optimize [pkgconfig get package-version]

	# load C version of Pages if available
	if {[pkgconfig get c-pages]} {
	    package require vfs::cookfs::c [pkgconfig get package-version]
    } {
	    package require vfs::cookfs::tcl::pages [pkgconfig get package-version]
	}

	# load C version of Fsindex if available
	if {[pkgconfig get c-fsindex]} {
	    package require vfs::cookfs::c [pkgconfig get package-version]
    } {
	    package require vfs::cookfs::tcl::fsindex [pkgconfig get package-version]
	}

    package require vfs::cookfs::tcl::readerchannel [pkgconfig get package-version]
	# load C version of Readerchannel if available
	if {[pkgconfig get c-readerchannel]} {
	    package require vfs::cookfs::c [pkgconfig get package-version]
    }

	set pkginitialized 1
    }

    # decide on crc32 implementation based on if zlib command is present
    if {(![catch {zlib crc32 ""} testvalue]) && ($testvalue == "0")} {
	proc ::cookfs::getCRC32 {v} {return [zlib crc32 $v]}
    }  else  {
	proc ::cookfs::getCRC32 {v} {package require crc32 ; return [crc::crc32 -format %d $v]}
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
	{noregister				     {Do not register this filesystem (for temporary writes)}}
	{nocommand				      {Do not create command for managing cookfs archive}}
	{bootstrap.arg		  ""	      {Bootstrap code to add in the beginning}}
	{compression.arg		"zlib"	  {Compression type to use}}
	{alwayscompress				 {Always compress}}
	{compresscommand.arg	    ""	      {Command to use for custom compression}}
	{asynccompresscommand.arg       ""	      {Command to use for custom, async compression}}
	{asyncdecompresscommand.arg       ""	      {Command to use for custom, async decompression}}
	{asyncdecompressqueuesize.arg     2           {Number of items to decompress in parallel at once}}
	{decompresscommand.arg	  ""	      {Command to use for custom decompression}}
	{endoffset.arg		  ""	      {Force reading VFS ending at specific offset instead of end of file}}
	{pagesobject.arg		""	      {Reuse existing pages object}}
	{fsindexobject.arg	      ""	      {Reuse existing fsindex object}}
	{setmetadata.arg		""	      {Set metadata after mounting}}
	{readonly				       {Open as read only}}
	{writetomemory				  {Open as read only and keep new files in memory}}
	{tcl-pages				      {Use Tcl based pages handler}}
	{tcl-fsindex				    {Use Tcl based fsindex handler}}
	{tcl-readerchannel			      {Use Tcl based readerchannel handler}}
	{pagesize.arg		   262144	  {Maximum page size}}
	{pagecachesize.arg	      8	       {Number of pages to cache}}
	{volume					 {Mount as volume}}
	{smallfilesize.arg	      32768	   {Maximum size of small files}}
	{smallfilebuffer.arg	    4194304	 {Maximum buffer for optimizing small files}}
	{nodirectorymtime				{Do not initialize mtime for new directories to current date and time}}
    }
    lappend options [list tempfilename.arg "" {Temporary file name to keep index in}]

    if { [pkgconfig get c-pages] || ![catch { package require md5 2 }] } {
        lappend options {pagehash.arg "md5" {Hash algorithm to use for page hashing}}
    } else {
        lappend options {pagehash.arg "crc32" {Hash algorithm to use for page hashing}}
    }

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

    if {![catch {package require Tcl 8.5}]} {
	set archive [file normalize [file join [pwd] $archive]]
	if {!$opt(volume)} {
	    set local [file normalize [file join [pwd] $local]]
	}
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
    }
    set fs(archive) $archive
    set fs(local) $local
    set fs(noregister) $opt(noregister)
    set fs(nocommand) $opt(nocommand)
    set fs(readonly) $opt(readonly)
    set fs(nodirectorymtime) $opt(nodirectorymtime)
    set fs(tclpages) [expr {![pkgconfig get c-pages] || $opt(tcl-pages)}]
    set fs(tclfsindex) [expr {![pkgconfig get c-fsindex] || $opt(tcl-fsindex)}]
    set fs(tclreaderchannel) [expr {$fs(tclpages) || ![pkgconfig get c-readerchannel] || $opt(tcl-readerchannel)}]

    # initialize pages
    if {$opt(pagesobject) == ""} {
	set pagesoptions [list -cachesize $opt(pagecachesize) \
	    -compression $opt(compression) \
	    -asynccompresscommand $opt(asynccompresscommand) \
	    -asyncdecompresscommand $opt(asyncdecompresscommand) \
	    -compresscommand $opt(compresscommand) \
	    -decompresscommand $opt(decompresscommand) \
	    -asyncdecompressqueuesize $opt(asyncdecompressqueuesize) \
	    ]

	if {$opt(readonly)} {
	    lappend pagesoptions -readonly
	}

	if {$opt(alwayscompress)} {
	    lappend pagesoptions -alwayscompress
	}

	if {$opt(endoffset) != ""} {
	    lappend pagesoptions -endoffset $opt(endoffset)
	}

	if {$fs(tclpages)} {
	    set pagescmd [list ::cookfs::tcl::pages]
	}  else  {
	    set pagescmd [list ::cookfs::pages]
	}

	set pagescmd [concat $pagescmd $pagesoptions [list $archive]]
	set fs(pages) [eval $pagescmd]
    }  else  {
	set fs(pages) $opt(pagesobject)
	$fs(pages) cachesize $opt(pagecachesize)
    }

    # initialize directory listing
    if {$opt(fsindexobject) == ""} {
	set idx [$fs(pages) index]
	if {$fs(tclfsindex)} {
	    set fsindexcmd [list ::cookfs::tcl::fsindex]
	}  else  {
	    set fsindexcmd [list ::cookfs::fsindex]
	}

	if {[string length $idx] > 0} {
	    lappend fsindexcmd $idx
	}
	set fs(index) [eval $fsindexcmd]
    }  else  {
	set fs(index) $opt(fsindexobject)
    }

    set fs(writer) [::cookfs::tcl::writer \
        -pages $fs(pages) -index $fs(index) \
        -smallfilebuffersize $opt(smallfilebuffer) \
        -smallfilesize $opt(smallfilesize) \
        -pagesize $opt(pagesize) \
        -writetomemory $opt(writetomemory)]

    # additional initialization if no pages currently exist
    if {[$fs(pages) length] == 0} {
	# add bootstrap if specified
	if {[string length $opt(bootstrap)] > 0} {
	    $fs(pages) add $opt(bootstrap)
	}

	$fs(pages) hash $opt(pagehash)

	$fs(index) setmetadata cookfs.pagehash $opt(pagehash)
    }  else  {
	# by default, md5 was the page hashing algorithm used
	$fs(pages) hash [$fs(index) getmetadata cookfs.pagehash "md5"]
    }

    foreach {paramname paramvalue} $opt(setmetadata) {
	$fs(index) setmetadata $paramname $paramvalue
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

    if {!$fs(nocommand)} {
	interp alias {} $fsid {} ::cookfs::handleVfsCommandAlias $fsid
    }

    return $fsid
}

proc cookfs::Unmount {fsid args} {
    upvar #0 $fsid fs

    # finalize any small files pending write
    $fs(writer) purge

    if {[$fs(index) changecount] && ![$fs(writer) writetomemory] && !$fs(readonly)} {
	$fs(pages) index [$fs(index) export]
    }

    # finalize writer, pages and index
    $fs(writer) delete
    set offset [$fs(pages) close]
    $fs(pages) delete
    $fs(index) delete

    # unmount filesystem
    if {!$fs(noregister)} {
	vfs::filesystem unmount $fs(local)
    }
    # remove command alias
    if {!$fs(nocommand)} {
	catch {rename $fsid ""}
    }

    unset $fsid

    return $offset
}

proc cookfs::handleVfsCommandAlias {fsid args} {
    upvar #0 $fsid fs

    if {[llength $args] == 0} {
	set ei "Usage: $fsid subcommand ?argument ...?"
	error $ei $ei
    }

    switch -- [lindex $args 0] {
	aside {
	    if {[llength $args] != 2} {
		set ei "wrong # args: should be \"$fsid aside filename\""
		error $ei $ei
	    }
	    return [::cookfs::aside $fsid [lindex $args 1]]
	}
	optimizelist {
	    if {[llength $args] != 3} {
		set ei "wrong # args: should be \"$fsid optimizelist base filelist\""
		error $ei $ei
	    }
	    return [::cookfs::optimizelist $fsid [lindex $args 1] [lindex $args 2]]
	}
	writetomemory {
	    if {[llength $args] != 1} {
		set ei "wrong # args: should be \"$fsid writetomemory\""
		error $ei $ei
	    }
	    return [::cookfs::writetomemory $fsid]
	}
	getmetadata {
	    if {![pkgconfig get feature-metadata]} { set ei "metadata not supported" ; error $ei $ei }
	    if {[llength $args] == 2} {
		return [$fs(index) getmetadata [lindex $args 1]]
	    }  elseif {[llength $args] == 3} {
		return [$fs(index) getmetadata [lindex $args 1] [lindex $args 2]]
	    }  else  {
		set ei "wrong # args: should be \"$fsid getmetadata property ?defaultValue?\""
		error $ei $ei
	    }
	}
	setmetadata {
	    if {![pkgconfig get feature-metadata]} { set ei "metadata not supported" ; error $ei $ei }
	    if {[llength $args] == 3} {
		if {$fs(readonly)} {
		    set ei "Archive is read-only"
		    error $ei $ei
		}
		return [$fs(index) setmetadata [lindex $args 1] [lindex $args 2]]
	    }  else  {
		set ei "wrong # args: should be \"$fsid setmetadata property value\""
		error $ei $ei
	    }
	}
	writeFiles {
	    set args [lrange $args 1 end]
	    if {([llength $args] % 4) != 0} {
		set ei "wrong # args: should be \"$fsid writeFiles ?filename1 type1 data1 size1 ?filename2 type2 data2 size2? ?..??"
		error $ei $ei
	    }
	    uplevel 1 [linsert $args 0 cookfs::writeFiles $fsid]
	}
	filesize {
	    if {[llength $args] != 1} {
		set ei "wrong # args: should be \"$fsid filesize\""
		error $ei $ei
	    }
	    return [$fs(pages) filesize]
	}
	smallfilebuffersize {
	    if {[llength $args] != 1} {
		set ei "wrong # args: should be \"$fsid smallfilebuffersize\""
		error $ei $ei
	    }
	    return [$fs(writer) smallfilebuffersize]
	}
	flush {
	    if {[llength $args] != 1} {
		set ei "wrong # args: should be \"$fsid flush\""
		error $ei $ei
	    }
	    $fs(writer) purge
	}
	compression {
	    if {[llength $args] == 1} {
		return [$fs(pages) compression]
	    }  elseif {[llength $args] == 2} {
		# always purge small files cache when compression changes
		$fs(writer) purge
		$fs(pages) compression [lindex $args 1]
	    }  else  {
		set ei "wrong # args: should be \"$fsid compression ?type?\""
		error $ei $ei
	    }
	}
	default {
	    set ei "unknown subcommand \"[lindex $args 0]\": must be one of aside, compression, filesize, flush, getmetadata, optimizelist, setmetadata, smallfilebuffersize, writeFiles or writetomemory."
	    error $ei $ei
	}
    }
}

proc cookfs::aside {fsid filename} {
    upvar #0 $fsid fs
    if {[$fs(writer) writetomemory]} {
	error "Write to memory option enabled; not creating add-aside archive"
    }

    $fs(writer) purge
    $fs(pages) aside $filename

    set newindex [cookfs::fsindex [$fs(pages) index]]
    $fs(index) delete
    set fs(index) $newindex
    $fs(writer) index $newindex
    set fs(readonly) 0
}

proc cookfs::writetomemory {fsid} {
    upvar #0 $fsid fs
    set fs(readonly) 0
    $fs(writer) writetomemory 1
}

proc cookfs::writeFiles {fsid args} {
    upvar #0 $fsid fs
    tailcall $fs(writer) write {*}$args
}

cookfs::initialize

package provide vfs::cookfs 1.6.0
