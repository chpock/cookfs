# cookfs benchmark
#
# Copyright (C) 2024 Konstantin Kushnir <chpock@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

package require vfs::cookfs

# Check requirements
foreach prog { gzip bzip2 xz zstd 7z } {
    if { ![string length [auto_execok $prog]] } {
        puts stderr "ERROR: could not find the required utility: $prog"
        set fail 1
    }
}
if { [info exists fail] } { exit 1 }

set selfdir [file dirname [info script]]
set pwd [pwd]

close [file tempfile tempdir]
file delete -force -- $tempdir
set tempdir [file join [file dirname $tempdir] cookfs-perm-temp]
file mkdir $tempdir
puts "Working directory: $tempdir"

source [file join $selfdir source.tcl]

if { [file exists [file join $selfdir data.tcl]] } {
    source [file join $selfdir data.tcl]
} {
    set data [dict create]
}

if { [file exists [file join $selfdir dishes.tcl]] } {
    source [file join $selfdir dishes.tcl]
} {
    set dishes [dict create]
}

proc getdirsize { dir } {
    set size  0
    set nfile 0
    set ndir  0
    foreach fn [glob -directory $dir -type f -nocomplain *] {
        if { $::tcl_platform(platform) eq "windows" } {
            # On Windows, 'file size' may fail on files like 'aux.c'. Let's try
            # to use unicode API for these cases.
            if { [catch { file size $fn } cursize] } {
                set cursize [file size "\\\\?\\$fn"]
            }
        } {
            set cursize [file size $fn]
        }
        incr size $cursize
        incr nfile
    }
    foreach fn [glob -directory $dir -type d -nocomplain *] {
        incr ndir
        lassign [getdirsize $fn] s f d
        incr size  $s
        incr nfile $f
        incr ndir  $d
    }
    return [list $size $nfile $ndir]
}

proc remsymlinks { dir } {
    file delete -force -- {*}[glob -directory $dir -type l -nocomplain *]
    foreach dir [glob -directory $dir -type d -nocomplain *] {
        remsymlinks $dir
    }
}

proc gethash { file } {
    set hash 0
    set fp [open $file r]
    fconfigure $fp -encoding binary -translation binary
    while { [string length [set data [read $fp 65536]]] } {
        set hash [zlib crc32 $data $hash]
    }
    close $fp
    return $hash
}

proc save { } {
    set fp [open [file join $::selfdir data.tcl] w]
    puts $fp "set data {"
    foreach hash [lsort [dict keys $::data]] {
        set data [dict get $::data $hash]
        puts $fp "    $hash {"
        foreach k [lsort [dict keys $data]] {
            set v [dict get $data $k]
            puts $fp "        [list $k $v]"
        }
        puts $fp "    }"
    }
    puts $fp "}"
    close $fp
}

proc save_dishes { } {
    set fp [open [file join $::selfdir dishes.tcl] w]
    puts $fp "set dishes {"
    foreach hash [lsort [dict keys $::dishes]] {
        set data [dict get $::dishes $hash]
        puts $fp "    $hash {"
        foreach k [lsort [dict keys $data]] {
            set v [dict get $data $k]
            puts $fp "        [list $k $v]"
        }
        puts $fp "    }"
    }
    puts $fp "}"
    close $fp
}

proc pack { cmd source } {
    set dest [file join $::tempdir "pack-test"]
    file delete -force $dest
    set cmd [string map [list @s $source @d $dest] $cmd]
    puts "Run: $cmd"
    exec {*}$cmd
    set ret [file size $dest]
    file delete -force $dest
    return $ret
}

proc cook { source args } {
    set dest [file join $::tempdir "pack-test"]
    file delete -force $dest
    puts "Cook with arguments: $args"
    ::cookfs::Mount $dest $dest {*}$args
    file copy $source $dest
    ::vfs::unmount $dest
    set ret [file size $dest]
    file delete -force $dest
    return $ret
}

dict for { title d } $archives {

    puts "Checking $title ..."
    if { ![dict exists $d hash] || ![file exists [file join $tempdir [dict get $d hash]]] } {
        if { ![dict exists $d hash] } {
            puts "Hash is not defined. Try to download the file..."
        } else {
            puts "File is not cached. Try to download the file..."
        }
        if { [catch { exec curl -L -o [file join $tempdir temp] [dict get $d url] 2>@1 } err] } {
            puts "ERROR: $err"
            exit 1
        }
        puts "The file successfully downloaded."
        set hash [gethash [file join $tempdir temp]]
        puts "Hash: $hash"
        file rename -force [file join $tempdir temp] [file join $tempdir $hash]
        if { ![dict exists $d hash] } {
            puts "WARNING: add that hash to source.tcl"
        } elseif { $hash ne [dict get $d hash] } {
            puts ""
            puts "WARNING: hash in source.tcl doesn't match. Update hash in the source.tcl."
            exit 1
        }
    } {
        set hash [dict get $d hash]
    }

    set packed [file join $tempdir $hash]
    set unpacked [file join $tempdir "unpacked-$hash"]

    puts "Unpack ..."
    file delete -force $unpacked
    file mkdir $unpacked
    cd $unpacked
    # wrap tar into catch to avoid symlink creation errors on Windows
    switch -- [dict get $d url_type] {
        tar.gz  { catch { exec tar xzf $packed } }
        tar.bz2 { catch { exec tar xjf $packed } }
        zip     {
            if { $::tcl_platform(platform) eq "windows" } {
                catch { exec tar xf $packed }
            } {
                exec unzip $packed
            }
        }
    }
    cd $pwd
    # remove symlinks as cookfs doesn't support them
    puts "Remove symlinks..."
    remsymlinks $unpacked
    puts "Unpacked."

    if { ![dict exists $data $hash unpacked_size] } {
        puts "Calculate the unpacked size..."
        lassign [getdirsize $unpacked] total nfiles ndirs
        puts "$total bytes in $nfiles files and $ndirs directories"
        dict set data $hash unpacked_size  $total
        dict set data $hash unpacked_files $nfiles
        dict set data $hash unpacked_dirs  $ndirs
        save
    }

    dict for { case_name case_data } $cases {
        if { [dict exists $data $hash $case_name] } continue
        puts "Check case '$case_name' for '$title'..."
        set size [pack [dict get $case_data cmd] $unpacked]
        puts "Got size: $size"
        dict set data $hash $case_name $size
        save
    }

    dict for { recipe recipe_data } $recipes {
        if { [dict exists $dishes $hash $recipe] } continue
        puts "Check dish '$recipe' for '$title'..."
        set size [cook $unpacked {*}[dict get $recipe_data args]]
        puts "Got size: $size"
        dict set dishes $hash $recipe $size
        save_dishes
    }

    puts "Cleanup ..."
    file delete -force $unpacked
    puts "Done."

}