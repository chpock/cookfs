proc randomData {bytes} {
    set rc {}
    for {set i 0} {$i < $bytes} {incr i 4} {
	append rc [binary format I [expr {wide(rand()*0x100000000)}]]
    }
    incr bytes -1
    return [string range $rc 0 $bytes]
}

proc randomDatas {count bytes} {
    set rc {}
    for {set i 0} {$i < $count} {incr i} {
	lappend rc [randomData $bytes]
    }
    return $rc
}

proc testIfEqual {a b} {
    if {(![file exists $a]) || (![file exists $b])} {
	return 0
    }

    if {[file mtime $a] != [file mtime $b]} {
	return 0
    }

    if {[file type $a] != [file type $b]} {
	return 0
    }

    set ok 1
    if {[file type $a] == "file"} {
	if {[file size $a] != [file size $b]} {
	    return 0
	}
	set afh [open $a r]
	set bfh [open $b r]
	while {![eof $afh]} {
	    set afc [read $afh 65536]
	    set bfc [read $bfh 65536]
	    if {![string equal $afc $bfc]} {
		set ok 0
		break
	    }
	}
	close $afh
	close $bfh
    }  elseif {[file type $a] == "directory"} {
	set g [concat [glob -nocomplain -directory $a *] [glob -nocomplain -directory $b *]]
	set g [lsort -unique $g]
	foreach g $g {
	    if {![testIfEqual [file join $a $g] [file join $b $g]]} {
		set ok 0
		break
	    }
	}
    }  else  {
	# TODO: add testing of symbolic links if cookfs ever supports them
    }

    return $ok
}

set testcompresscount 0
set testdecompresscount 0
set testasynccompresscount 0

proc testcompress {d} {
    incr ::testcompresscount
    binary scan [vfs::zip -mode compress $d] H* rc
    return "HEXTEST-$rc"
}
proc testdecompress {d} {
    incr ::testdecompresscount
    set rc [vfs::zip -mode decompress [binary format H* [string range $d 8 end]]]
    return $rc
}

proc testasynccompress {cmd idx arg} {
    if {$cmd == "init"} {
	set ::testasyncqueue {}
	set ::testasyncqueuesize $idx
	set ::testasynccompresscount 0
    } elseif {$cmd == "compress"} {
	incr ::testasynccompresscount
	lappend ::testasyncqueue $idx [testcompress $arg]
    } elseif {$cmd == "wait"} {
	incr ::testasynccompresscount
	if {$arg || ([llength $::testasyncqueue] >= $::testasyncqueuesize)} {
	    set rc [lrange $::testasyncqueue 0 1]
	    set ::testasyncqueue [lrange $::testasyncqueue 2 end]
	    return $rc 
	}
	return {}
    }
}

set testcompresscountraw 0
set testdecompresscountraw 0

proc testcompressraw {d} {
    incr ::testcompresscountraw
    binary scan $d H* rc
    return "RAWTEST-$rc"
}
proc testdecompressraw {d} {
    incr ::testdecompresscountraw
    set rc [binary format H* [string range $d 8 end]]
    return $rc
}

