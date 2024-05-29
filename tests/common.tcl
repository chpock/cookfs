if {[info exists ::env(DEBUG)]} {
    proc cookfs::debug {code} {puts [uplevel 1 [list subst $code]]}
}

proc fsindexEqual { a b } {
    if { [lrange $a 1 end] eq [lrange $b 1 end] } {
        if { abs([lindex $a 0] - [lindex $b 0]) < 5 } {
            return 1
        }
    }
    return 0
}

proc makeTree { dir tree } {
    file mkdir $dir
    foreach { type name data } $tree {
        switch -glob -- $type {
            f* {
                if { [string index $name end] eq "%" } {
                    set name [string range $name 0 end-1]
                    set filler [randomData $data]
                } else {
                    set filler [string repeat "a" $data]
                }
                set fp [open [file join $dir $name] w]
                fconfigure $fp -translation binary
                puts -nonewline $fp $filler
                close $fp
            }
            d* {
                makeTree [file join $dir $name] $data
            }
        }
    }
}

proc makeSimpleTree { dir } {
    makeTree $dir {
        file rootfile1 10
        file emptyfile 0
        file bigfile 0xffffff
        dir firstsubd {
            file emptyfile 0
            file bigfile 0xfff1
            dir 2ndfir {
                file "with spaces.txt" 20
                dir "with spaces" {
                    file null 0
                }
                dir emptydir {
                }
            }
        }
        dir anotherdir {
            file foobar 1
        }
    }
}

# expects -smallfilesize 0x10 -smallfilebuffer 0x30 -pagesize 0x10
proc makeSimpleTree2 { dir } {
    makeTree $dir {
        file big-two-pages.a% 0x20
        file medium-one-page.a% 0x10
        file rootfile 3
        file small-half-page.a% 0x8
        file very-bigfile% 0x100
        dir onedir {
            file empty-file 0
            file big-two-pages.b% 0x20
            dir twodir {
                file very-small-one% 1
                file very-small-two% 2
                file "with spaces.txt" 2
                dir "with spaces" {
                    file null 0
                    file very-small-one.f% 1
                }
                dir emptydir {
                }
                file very-small-three% 3
                file small-half-page.c% 0x8
            }
            file small-half-page.b% 0x8
        }
        file medium-one-page.b% 0x10
        dir anotherdir {
            file foobar 1
        }
        file emptyfile 0
        file medium-one-page.c% 0x10
    }
}

proc randomData {bytes} {
    set rc {}
    # speed up random data generation by duplicating one random fragment
    if { $bytes > 512 } {
        set fragment [randomData 508]
        for {set i 0} {$i < $bytes} {incr i 512} {
            append rc $fragment [binary format I [expr {wide(rand()*0x100000000)}]]
        }
    } else {
        for {set i 0} {$i < $bytes} {incr i 4} {
            append rc [binary format I [expr {wide(rand()*0x100000000)}]]
        }
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

proc getFilesRelative { dir } {
    set files [lsort [glob -nocomplain -tails -type f -directory $dir *]]
    foreach subdir [lsort [glob -nocomplain -type d -directory $dir *]] {
        foreach f [getFilesRelative $subdir] {
            lappend files [file join [file tail $subdir] $f]
        }
    }
    return $files
}

proc testOptimizedList { base files fsindex } {
    # We expect small files with increasing block number to go first.
    # Then the large files will follow.
    set result [list]
    set lastPage 0
    set lastSmall 1

    foreach { file } $files {
        set file [file join $base $file]
        set chunklist [lindex [$fsindex get $file] 2]
        set isSmall [expr { [llength $chunklist] == 3 }]
        set onPage [lindex $chunklist 0]
        if { $isSmall } {
            # puts "small $file on $onPage"
            if { $onPage < $lastPage } {
                lappend result "Unexpected file \"$file\" on page #$onPage when previous page was $lastPage"
            }
            if { !$lastSmall } {
                lappend result "Unexpected small file \"$file\" when lage files are expected"
            }
            set lastPage $onPage
        } else {
            # puts "large $file on $onPage"
            set lastSmall 0
        }
    }

}

proc testIfEqual {a b} {
    if {(![file exists $a]) || (![file exists $b])} {
        return "file exists $a: [file exists $a]\nfile exists $b: [file exists $b]"
    }

    # The 1-10 second difference is possible because 'file copy' doesn't copy
    # mtime from source to destination. Thus, it is possible that the mtime
    # in the source test data we just prepared is different from the mtime
    # at the destination. However, we should catch a time difference of more
    # than 10 seconds because that means something is wrong with
    # the destination.
    if { abs([file mtime $a] - [file mtime $b]) > 10 } {
        return "file mtime $a: [file mtime $a]\nfile mtime $b: [file mtime $b]"
    }

    if {[file type $a] != [file type $b]} {
        return "file type $a: [file type $a]\nfile type $b: [file type $b]"
    }

    if {[file type $a] == "file"} {
        if {[file size $a] != [file size $b]} {
            return "file size $a: [file size $a]\nfile size $b: [file size $b]"
        }
        set afh [open $a r]
        fconfigure $afh -translation binary
        set bfh [open $b r]
        fconfigure $bfh -translation binary
        while {![eof $afh]} {
            set afc [read $afh 65536]
            set bfc [read $bfh 65536]
            if {![string equal $afc $bfc]} {
                return "file contents $a: $afc\nfile contents $b: $bfc"
            }
        }
        close $afh
        close $bfh
    }  elseif {[file type $a] == "directory"} {
        set g [concat [glob -nocomplain -directory $a *] [glob -nocomplain -directory $b *]]
        set g [lsort -unique $g]
        foreach g $g {
            if {[set ok [testIfEqual [file join $a $g] [file join $b $g]]] ne "1"} {
                return $ok
            }
        }
    }  else  {
        # TODO: add testing of symbolic links if cookfs ever supports them
    }

    return "1"
}

proc testcompresscleanup {} {
    unset -nocomplain ::testcompresscount ::testdecompresscount
}

proc testcompress {d {count 1}} {
    incr ::testcompresscount $count
    binary scan [vfs::zip -mode compress $d] H* rc
    return "HEXTEST-$rc"
}
proc testdecompress {d {count 1}} {
    incr ::testdecompresscount $count
    set rc [vfs::zip -mode decompress [binary format H* [string range $d 8 end]]]
    return $rc
}

proc testasynccleanup {} {
    unset -nocomplain ::testasynccompressqueue ::testasynccompressqueuesize ::testasynccompressfinalized
    unset -nocomplain ::testasyncdecompressqueue ::testasyncdecompressqueuesize ::testasyncdecompressfinalized
    unset -nocomplain ::testasynccompresscount ::testasyncdecompresscount
    unset -nocomplain ::testasyncdecompresswaitcount
    unset -nocomplain ::testasyncdecompressprocesscount
    testcompresscleanup
}

proc testasynccompress {cmd idx arg} {
    set rc {}
    if {[catch {
        if {$cmd == "init"} {
            set ::testasynccompressqueue {}
            set ::testasynccompressqueuesize $idx
            set ::testasynccompresscount 0
            set ::testasynccompressfinalized 0
        } elseif {$cmd == "process"} {
            incr ::testasynccompresscount
            lappend ::testasynccompressqueue $idx [testcompress $arg 0]
        } elseif {$cmd == "wait"} {
            incr ::testasynccompresscount
            if {$arg || ([llength $::testasynccompressqueue] >= $::testasynccompressqueuesize)} {
                set rc [lrange $::testasynccompressqueue 0 1]
                set ::testasynccompressqueue [lrange $::testasynccompressqueue 2 end]
            }
        } elseif {$cmd == "finalize"} {
            set ::testasynccompressfinalized 1
        }
    } err]} {
        cookfs::debug {Error in testasynccompress: $::errorInfo}
    }
    return $rc
}

proc testasyncdecompress {cmd idx arg} {
    set rc {}
    if {[catch {
        if {$cmd == "init"} {
            set ::testasyncdecompressqueue {}
            set ::testasyncdecompressqueuesize $idx
            set ::testasynccompresscount 0
            set ::testasyncdecompressfinalized 0
        } elseif {$cmd == "process"} {
            incr ::testasynccompresscount
            lappend ::testasyncdecompressqueue $idx [testdecompress $arg 0]
        } elseif {$cmd == "wait"} {
            incr ::testasynccompresscount
            if {$arg || ([llength $::testasyncdecompressqueue] >= 2)} {
                set rc [lrange $::testasyncdecompressqueue 0 1]
                set ::testasyncdecompressqueue [lrange $::testasyncdecompressqueue 2 end]
            }
        } elseif {$cmd == "finalize"} {
            set ::testasyncdecompressfinalized 1
        }
    } err]} {
        cookfs::debug {Error in testasyncdecompress: $::errorInfo}
    }
    return $rc
}

proc testasyncdecompressrandom {cmd idx arg} {
    set rc {}
    if {[catch {
        if {$cmd == "init"} {
            set ::testasyncdecompressqueue {}
            set ::testasyncdecompressqueuesize $idx
            set ::testasyncdecompresscount 0
            set ::testasyncdecompressfinalized 0
            set ::testasyncdecompressprocesscount 0
            set ::testasyncdecompresswaitcount 0
        } elseif {$cmd == "process"} {
            incr ::testasyncdecompresscount
            incr ::testasyncdecompressprocesscount
            lappend ::testasyncdecompressqueue $idx [testdecompress $arg 0]
        } elseif {$cmd == "wait"} {
            incr ::testasyncdecompresscount
            incr ::testasyncdecompresswaitcount
            if {$arg || ([llength $::testasyncdecompressqueue] >= 2)} {
                set i [expr {int(rand() * [llength $::testasyncdecompressqueue] / 2) * 2}]
                set rc [lrange $::testasyncdecompressqueue $i [expr {$i + 1}]]
                set ::testasyncdecompressqueue [lreplace $::testasyncdecompressqueue $i [expr {$i + 1}]]
            }
        } elseif {$cmd == "finalize"} {
            set ::testasyncdecompressfinalized 1
        }
    } err]} {
        cookfs::debug {Error in testasyncdecompress: $::errorInfo}
    }
    return $rc
}

proc testrawcleanup {} {
    unset -nocomplain ::testcompresscountraw ::testdecompresscountraw
}

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
