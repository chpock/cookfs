# Handling for Tcl VFS functionality.
#
# (c) 2010 Wojciech Kocjan, Pawel Salawa
# (c) 2011-2014 Wojciech Kocjan
# (c) 2024 Konstantin Kushnir

package require vfs

namespace eval cookfs {}
namespace eval cookfs::tcl {}
namespace eval cookfs::tcl::vfs {
    variable mountId 0
    variable attributes [list "-vfs" "-handle"]
}

if { ![llength [info commands ::cookfs::debug]] } {
    proc cookfs::debug {args} {}
}
# Mount VFS - usage:
# cookfs::Mount ?options? archive local
# Run cookfs::Mount -help for a description of all options
#
proc cookfs::tcl::Mount {args} {
    variable mountId
    #
    # Parse options and get paths from args variable
    #
    set options {
	{noregister				     {Do not register this filesystem (for temporary writes)}}
	{nocommand				      {Do not create command for managing cookfs archive}}
	{bootstrap.arg		  ""	      {Bootstrap code to add in the beginning}}
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
	{tcl-writerchannel			      {Use Tcl based writerchannel handler}}
	{pagesize.arg		   262144	  {Maximum page size}}
	{pagecachesize.arg	      8	       {Number of pages to cache}}
	{volume					 {Mount as volume}}
	{smallfilesize.arg	      32768	   {Maximum size of small files}}
	{smallfilebuffer.arg	    4194304	 {Maximum buffer for optimizing small files}}
	{nodirectorymtime				{Do not initialize mtime for new directories to current date and time}}
    }

    if { [::cookfs::pkgconfig get feature-xz] } {
        lappend options {compression.arg "xz" {Compression type to use}}
    } else {
        lappend options {compression.arg "zlib" {Compression type to use}}
    }

    if { [::cookfs::pkgconfig get c-pages] || ![catch { package require md5 2 }] } {
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

    set fsid ::cookfs::tcl::vfs::mount[incr vfs::mountId]

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
    set fs(tclpages) [expr {![::cookfs::pkgconfig get c-pages] || $opt(tcl-pages)}]
    set fs(tclfsindex) [expr {![::cookfs::pkgconfig get c-fsindex] || $opt(tcl-fsindex)}]
    set fs(tclreaderchannel) [expr {$fs(tclpages) || ![::cookfs::pkgconfig get c-readerchannel] || $opt(tcl-readerchannel)}]
    set fs(tclwriterchannel) [expr {$fs(tclpages) || $fs(tclfsindex) || ![::cookfs::pkgconfig get c-writerchannel] || $opt(tcl-writerchannel)}]

    # load Tcl packages if we need them but don't currently have them
    if {$fs(tclpages) && [catch {package present vfs::cookfs::tcl::pages}]} {
        package require cookfs::tcl::pages [::cookfs::pkgconfig get package-version]
    }
    if {$fs(tclfsindex) && [catch {package present vfs::cookfs::tcl::fsindex}]} {
        package require cookfs::tcl::fsindex [::cookfs::pkgconfig get package-version]
    }
    if {$fs(tclreaderchannel) && [catch {package present vfs::cookfs::tcl::readerchannel}]} {
        package require cookfs::tcl::readerchannel [::cookfs::pkgconfig get package-version]
    }
    if {$fs(tclwriterchannel) && [catch {package present vfs::cookfs::tcl::writerchannel}]} {
        package require cookfs::tcl::writerchannel [::cookfs::pkgconfig get package-version]
    }

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
	    set pagescmd [list ::cookfs::c::pages]
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
	    set fsindexcmd [list ::cookfs::c::fsindex]
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

    lappend mountcommand $local [list cookfs::tcl::vfs::handler $fsid]
    if {!$fs(noregister)} {
	eval $mountcommand
	set unmountcmd [list ::cookfs::tcl::Unmount -unregister $fsid]
	if {[info commands ::vfs::RegisterMount] != ""} {
	    vfs::RegisterMount $local $unmountcmd
	}  else  {
	    set ::vfs::_unmountCmd([file normalize $local]) $unmountcmd
	}
    }

    if {!$fs(nocommand)} {
	interp alias {} $fsid {} ::cookfs::tcl::vfs::mountHandler $fsid
    }

    return $fsid
}

proc cookfs::tcl::Unmount {args} {

    if { [lindex $args 0] eq "-unregister" } {
        # If the first argument is "-unregister", then we called from
        # vfs::unmount. fsid is the 2nd argument.
        set fsid [lindex $args 1]
    } else {
        if { [string match {*cookfs::tcl::vfs::mount*} [lindex $args 0]] && [info exists [lindex $args 0]] } {
            # If the first argument looks like *cookfs::tcl::vfs::mount*
            # and this variable exists, consider it fsid
            set fsid [lindex $args 0]
            upvar #0 $fsid fs
            set mount_point $fs(local)
        } else {
            # In other cases, treat it as a mount path and call vfs::unmount to unmount it
            set mount_point [lindex $args 0]
        }
        # call vfs::unmount to properly unmount the vfs
        return [vfs::unmount $mount_point]
    }

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

proc cookfs::tcl::vfs::mountHandler {fsid args} {
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
            if {[$fs(writer) writetomemory]} {
                error "Write to memory option enabled; not creating add-aside archive"
            }
            $fs(writer) purge
            $fs(pages) aside [lindex $args 1]
            set newindex [cookfs::fsindex [$fs(pages) index]]
            $fs(index) delete
            set fs(index) $newindex
            $fs(writer) index $newindex
            set fs(readonly) 0
            return
	}
	optimizelist {
	    if {[llength $args] != 3} {
		set ei "wrong # args: should be \"$fsid optimizelist base filelist\""
		error $ei $ei
	    }
	    return [optimizelist $fsid [lindex $args 1] [lindex $args 2]]
	}
	writetomemory {
	    if {[llength $args] != 1} {
		set ei "wrong # args: should be \"$fsid writetomemory\""
		error $ei $ei
	    }
            set fs(readonly) 0
            $fs(writer) writetomemory 1
            return
	}
	getmetadata {
	    if {![::cookfs::pkgconfig get feature-metadata]} {
	        set ei "metadata not supported"
	        error $ei $ei
	    }
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
	    if {![::cookfs::pkgconfig get feature-metadata]} {
	        set ei "metadata not supported"
	        error $ei $ei
	    }
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
            tailcall $fs(writer) write {*}$args
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
	getpages {
	    if {[llength $args] != 1} {
		set ei "wrong # args: should be \"$fsid getpages\""
		error $ei $ei
	    }
	    return $fs(pages)
	}
	getindex {
	    if {[llength $args] != 1} {
		set ei "wrong # args: should be \"$fsid getindex\""
		error $ei $ei
	    }
	    return $fs(index)
	}
	getwriter {
	    if {[llength $args] != 1} {
		set ei "wrong # args: should be \"$fsid getwriter\""
		error $ei $ei
	    }
	    return $fs(writer)
	}
	default {
	    set ei "unknown subcommand \"[lindex $args 0]\": must be one of aside, compression, filesize, flush, getmetadata, optimizelist, setmetadata, smallfilebuffersize, writeFiles or writetomemory."
	    error $ei $ei
	}
    }
}

proc cookfs::tcl::vfs::optimizelist {fsid base filelist} {
    upvar #0 $fsid fs

    set largefiles {}
    set smallfiles {}

    foreach file $filelist {
	set path [file join $base $file]
	set chunklist [lindex [$fs(index) get $path] 2]
	if {[llength $chunklist] > 3} {
	    lappend largefiles $file
	}  else  {
	    lappend atpage([lindex $chunklist 0]) $file
	}
    }

    foreach idx [lsort -integer [array names atpage]] {
	set smallfiles [concat $smallfiles $atpage($idx)]
    }

    return [concat $smallfiles $largefiles]
}

# error codes used in CookFS (used for bootstraping CooKit)
set cookfs::posix(ENOENT) 2
set cookfs::posix(ENOTDIR) 20
set cookfs::posix(EISDIR) 21
set cookfs::posix(EROFS) 30
set cookfs::posix(EINVAL) 22

# dispatcher for commands
proc cookfs::tcl::vfs::handler {fsid cmd root relative actualpath args} {
    upvar #0 $fsid fs

    #vfs::log [list cookfs::vfshandler $fsid $cmd $relative $args]

    set rc [list]
    # TODO: rewrite everything to separate commands
    switch -- $cmd {
        createdirectory {
            # create directory
            if {[catch {
                createdirectory $fsid $root $relative $actualpath
            } error]} {
                #vfs::log [list cookfs::vfshandler createdirectory $fsid $relative $error]
                vfs::filesystem posixerror $::cookfs::posix(ENOTDIR)
            }
        }
        deletefile {
            set rc [delete $fsid $root $relative $actualpath file false]
        }
        removedirectory {
            set rc [delete $fsid $root $relative $actualpath directory [lindex $args 0]]
        }
        fileattributes {
            set rc [fileattributes \
                $fsid $root $relative $actualpath $args]
        }
        matchindirectory {
            set rc [matchindirectory $fsid \
                $relative $actualpath [lindex $args 0] [lindex $args 1]]
        }
        stat {
            set rc [stat $fsid $relative $actualpath]
        }
        open {
            # we skip permissions as we don't really have on
            set rc [open $fsid $relative [lindex $args 0]]
        }
        access {
            stat $fsid $relative $actualpath
            set rc true
        }
        utime {
            # modify mtime and atime, assuming file exists
            if {[catch {stat $fsid $relative $actualpath} rc]} {
                vfs::filesystem posixerror $::cookfs::posix(ENOENT)
            }

            $fs(index) setmtime $relative [lindex $args 1]
        }
    }
    return $rc
}

proc cookfs::tcl::vfs::createdirectory {fsid root relative actualpath} {
    upvar #0 $fsid fs

    if {$fs(nodirectorymtime)} {
        set clk 0
    }  else  {
        set clk [clock seconds]
    }
    $fs(index) set $relative $clk
}

# handle file attributes
proc cookfs::tcl::vfs::fileattributes {fsid root relative actualpath a} {
    upvar #0 $fsid fs
    variable attributes
    switch -- [llength $a] {
        0 {
            # list strings
            #return [::vfs::listAttributes]
            if { [string length $relative] } {
                return [lrange $attributes 0 0]
            } else {
                return $attributes
            }
        }
        1 {
            # get value
            set index [lindex $a 0]
            switch -exact [lindex $attributes $index] {
                -vfs {
                    return 1
                }
                -handle {
                    if { [llength [info commands $fsid]] } {
                        return $fsid
                    } else {
                        return ""
                    }
                }
            }
            vfs::filesystem posixerror $cookfs::posix(EINVAL)
        }
        2 {
            return -code error "attributes of CookFS objects are read-only"
            # set value
            #if {0} {
            #    # handle read-only
            #    vfs::filesystem posixerror $::cookfs::posix(EROFS)
            #}
            #set index [lindex $a 0]
            #set val [lindex $a 1]
            #return [::vfs::attributesSet $root $relative $index $val]
        }
    }
}

# implementation of stat
proc cookfs::tcl::vfs::stat {fsid relative actualpath} {
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
proc cookfs::tcl::vfs::matchindirectory {fsid relative actualpath pattern types} {
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
proc cookfs::tcl::vfs::delete {fsid root relative actualpath type recursive} {
    upvar #0 $fsid fs
    #vfs::log [list cookfs::vfshandleDelete $fsid $relative $type $recursive]
    if {[catch {stat $fsid $relative $actualpath} rc]} {
        vfs::filesystem posixerror $::cookfs::posix(ENOENT)
    }

    array set a $rc
    if {$a(type) != $type} {
        if {$type == "file"} {
            vfs::filesystem posixerror $::cookfs::posix(EISDIR)
        }  else  {
            vfs::filesystem posixerror $::cookfs::posix(ENOTDIR)
        }
    } elseif {$type eq "file"} {
        $fs(writer) deleteFile $relative
    } elseif {$type == "directory" && $recursive} {
        if {[catch {
            foreach ch [$fs(index) list $relative] {
                # check type and delete appropriately
                set g [$fs(index) get [file join $relative $ch]]
                if {[llength $g] == 1} {
                    delete $fsid $root [file join $relative $ch] [file join $actualpath $ch] "directory" 1
                }  else  {
                    delete $fsid $root [file join $relative $ch] [file join $actualpath $ch] "file" 0
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
proc cookfs::tcl::vfs::open {fsid relative mode} {
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
                $fs(index) get $relative
            } fileinfo]} {
                vfs::filesystem posixerror $::cookfs::posix(ENOENT)
            }
            if {[llength $fileinfo] != 3} {
                vfs::filesystem posixerror $::cookfs::posix(EISDIR)
            }
            foreach {mtime size chunklist} $fileinfo break
            # if this is a small file, currently pending write, pass it to memchan
            if {([llength $chunklist] == 3) && ([lindex $chunklist 0] < 0)} {
                if {$fs(tclwriterchannel)} {
                    set channel [lindex [::cookfs::tcl::writerchannel $fsid $relative true] 0]
                } else {
                    set channel [::cookfs::c::writerchannel $fs(pages) $fs(index) $fs(writer) $relative true]
                }
            } elseif {$fs(tclreaderchannel)} {
                set channel [::cookfs::tcl::readerchannel $fsid $chunklist]
            } else {
                set channel [::cookfs::c::readerchannel $fs(pages) $fs(index) $chunklist]
            }
            return [list $channel ""]
        }
        r+ {
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative read+]

            if {$fs(tclwriterchannel)} {
                set rc [::cookfs::tcl::writerchannel $fsid $relative true]
            } else {
                set rc [list [::cookfs::c::writerchannel $fs(pages) $fs(index) $fs(writer) $relative true] ""]
            }

            #vfs::log [list cookfs::vfshandleOpen $fsid $relative result $rc]
            return $rc
        }
        w - w+ {
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative write]

            if {$fs(tclwriterchannel)} {
                set rc [::cookfs::tcl::writerchannel $fsid $relative false]
            } else {
                set rc [list [::cookfs::c::writerchannel $fs(pages) $fs(index) $fs(writer) $relative false] ""]
            }

            #vfs::log [list cookfs::vfshandleOpen $fsid $relative result $rc]
            return $rc
        }
        a - a+ {
            #vfs::log [list cookfs::vfshandleOpen $fsid $relative append]
            if {[catch {stat $fsid $relative $actualpath} rc]} {
                vfs::filesystem posixerror $::cookfs::posix(ENOENT)
            }

            if {$fs(tclwriterchannel) } {
                set rc [::cookfs::tcl::writerchannel $fsid $relative true]
            } else {
                set rc [list [::cookfs::c::writerchannel $fs(pages) $fs(index) $fs(writer) $relative true] ""]
            }

            seek [lindex $rc 0] 0 end

            #vfs::log [list cookfs::vfshandleOpen $fsid $relative result $rc]
            return $rc
        }
        default {
            error "Unknown mode $mode"
        }
    }
}

if { ![llength [info commands cookfs::Mount]] } {
    interp alias {} cookfs::Mount {} cookfs::tcl::Mount
    interp alias {} cookfs::Unmount {} cookfs::tcl::Unmount
}

if { ![llength [info commands vfs::cookfs::Mount]] } {
    interp alias {} vfs::cookfs::Mount {} cookfs::tcl::Mount
}

package provide cookfs::tcl::vfs 1.6.0
