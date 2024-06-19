# Allows creation of uncompressed cookfs archive
# from any Tcl version starting with 8.3
#
# Mainly used for creating a temporary CooKit binary
# that will use full cookfs to rewrite archive

namespace eval cookfs {}

proc cookfs::createArchiveFileIndex {filelist} {
    set rc ""

    append rc [binary format I [expr {[llength $filelist] / 4}]]
    foreach {name mtime v1 v2} $filelist {
        # TODO: handle 64-bit mtime
        set filename [binary format cA*cII [string length $name] $name 0 0 $mtime]
        if {$v1 >= 0} {
            # file - add 1 page with v1 being pageid and v2 being size
            append rc $filename [binary format IIII 1 $v1 0 $v2]
        }  else  {
            # directory - v2 contains filelist for this directory
            append rc $filename [binary format I -1] [createArchiveFileIndex $v2]
        }
    }

    return $rc
}

proc cookfs::createArchiveAddFilePath {var pathlist name clk v1 v2} {
    upvar 1 $var idx
    if {[llength $pathlist] == 0} {
        set newidx [list]
        foreach {iname imtime iv1 iv2} $idx {
            if {$iname == $name} {
                continue
            }
            lappend newidx $iname $imtime $iv1 $iv2
        }
        lappend newidx $name $clk $v1 $v2
        set idx $newidx
    }  else  {
        set done 0
        set newidx [list]
        set pathrest [lrange $pathlist 1 end]
        set pathhead [lindex $pathlist 0]

        foreach {iname imtime iv1 iv2} $idx {
            if {$iname == $pathhead} {
                if {$iv1 < 0} {
                    createArchiveAddFilePath iv2 $pathrest $name $clk $v1 $v2
                    set done 1
                }  else  {
                    error "$iname is not a directory"
                }
            }
            lappend newidx $iname $imtime $iv1 $iv2
        }

        if {!$done} {
            set iv2 [list]
            createArchiveAddFilePath iv2 $pathrest $name $clk $v1 $v2
            lappend newidx $pathhead [clock seconds] -1 $iv2
        }
        set idx $newidx
    }
}

proc cookfs::createArchiveAddFile {path clk v1 v2} {
    upvar 1 fileindex fileindex
    set pl [split $path /\\]
    set name [lindex $pl end]

    createArchiveAddFilePath fileindex [lrange $pl 0 end-1] $name $clk $v1 $v2
}

proc cookfs::createArchive {archivefile filelist {bootstrap ""}} {
    set index [list]

    set pagelist [list]
    set pageidx 0

    if {[string length $bootstrap] > 0} {
        lappend pagelist $bootstrap
        incr pageidx
    }

    set fileindex [list]

    foreach {path datatype data size} $filelist {
        if {$size == ""} {
            # read actual size, from file or channel
            switch -- $datatype {
                file {
                    set size [file size $data]
                }
                channel {
                    set tmp [tell $data]
                    seek $data 0 end
                    set size [tell $data]
                    seek $data $tmp start
                }
                data {
                    set size [string length $data]
                }
                default {
                    error "Unknown data type: $datatype"
                }
            }
        }

        # get actual channel and whether it should be closed
        switch -- $datatype {
            file {
                set clk [file mtime $data]
                set chan [open $data r]
                fconfigure $chan -translation binary
                set doclose 1
                set rawdata 0
            }
            channel {
                set clk [clock seconds]
                set chan $data
                fconfigure $chan -translation binary
                set doclose 0
                set rawdata 0
            }
            data {
                set clk [clock seconds]
                set doclose 0
                set rawdata 1
            }
            default {
                error "Unknown data type: $datatype"
            }
        }

        if {!$rawdata} {
            set data [read $chan]
            if {$doclose} {close $chan}
        }

        lappend pagelist $data
        createArchiveAddFile $path $clk $pageidx [string length $data]
        incr pageidx
    }

    if {[file exists $archivefile]} {
        set fh [open $archivefile a]
    }  else  {
        set fh [open $archivefile w]
    }
    fconfigure $fh -translation binary
    foreach page $pagelist {
        puts -nonewline $fh \u0000$page
    }

    # add fake md5 indexes
    foreach page $pagelist {
        puts -nonewline $fh [binary format IIII 0 0 0 0]
    }

    # add page indexes
    foreach page $pagelist {
        puts -nonewline $fh [binary format I [expr {[string length $page] + 1}]]
    }

    # TODO: add index
    set indexdata "\u0000CFS2.200[createArchiveFileIndex $fileindex]"

    puts -nonewline $fh $indexdata
    puts -nonewline $fh [binary format IIcca* [string length $indexdata] [llength $pagelist] 0 0 CFS0002]
    close $fh
}

