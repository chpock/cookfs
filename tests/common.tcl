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

