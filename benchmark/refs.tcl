# cookfs benchmark
#
# Copyright (C) 2024 Konstantin Kushnir <chpock@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

set selfdir [file dirname [info script]]

source [file join $selfdir source.tcl]

if { [file exists [file join $selfdir data.tcl]] } {
    source [file join $selfdir data.tcl]
} {
    puts stderr "No data available."
    exit 1
}

if { [file exists [file join $selfdir dishes_prev.tcl]] } {
    source [file join $selfdir dishes_prev.tcl]
} {
    puts stderr "No prev-dishes available."
    exit 1
}

if { [file exists [file join $selfdir dishes.tcl]] } {
    source [file join $selfdir dishes.tcl]
} {
    puts stderr "No dishes available."
    exit 1
}

proc getmaxlength { list } {
    return [lindex [lsort -integer [lmap x $list {string length $x}]] end]
}

foreach case [dict keys $cases] {
    if { ![dict exists $cases $case hide] } continue
    dict unset cases $case
}

set maxtitle [getmaxlength [dict keys $archives]]
set maxcase  [getmaxlength [dict keys $cases]]

set template_title "%${maxtitle}s"
set template_row   $template_title

append template_title [string repeat " %${maxcase}s" [dict size $cases]]
append template_row   [string repeat " %${maxcase}.2f" [dict size $cases]]

puts [format $template_title "" {*}[dict keys $cases]]

foreach archive [lsort [dict keys $archives]] {

    set hash [dict get $archives $archive hash]
    set unpacked [dict get $data $hash unpacked_size]
    set ratio_base [expr { 1.0 * [dict get $data $hash gzip] / $unpacked }]

    set numbers [list]
    foreach case [dict keys $cases] {
        set size [dict get $data $hash $case]
        set ratio [expr { (100.0 * $size / $unpacked) / $ratio_base }]
        lappend numbers $ratio
    }

    puts [format $template_row $archive {*}$numbers]

}

puts ""
puts "Dishes prev:"
puts ""

set maxrecipe [getmaxlength [dict keys $recipes]]

set template_title "%${maxtitle}s"
set template_row   $template_title

append template_title [string repeat " %${maxcase}s" [dict size $recipes]]
append template_row   [string repeat " %${maxcase}.2f" [dict size $recipes]]

puts [format $template_title "" {*}[dict keys $recipes]]

foreach archive [lsort [dict keys $archives]] {

    set hash [dict get $archives $archive hash]
    set unpacked [dict get $data $hash unpacked_size]
    set ratio_base [expr { 1.0 * [dict get $data $hash gzip] / $unpacked }]

    set numbers [list]
    foreach recipe [dict keys $recipes] {
        set size [dict get $dishes_prev $hash $recipe]
        set ratio [expr { (100.0 * $size / $unpacked) / $ratio_base }]
        lappend numbers $ratio
    }

    puts [format $template_row $archive {*}$numbers]

}

puts ""
puts "Dishes now:"
puts ""

foreach archive [lsort [dict keys $archives]] {

    set hash [dict get $archives $archive hash]
    set unpacked [dict get $data $hash unpacked_size]
    set ratio_base [expr { 1.0 * [dict get $data $hash gzip] / $unpacked }]

    set numbers [list]
    foreach recipe [dict keys $recipes] {
        set size [dict get $dishes $hash $recipe]
        set ratio [expr { (100.0 * $size / $unpacked) / $ratio_base }]
        lappend numbers $ratio
    }

    puts [format $template_row $archive {*}$numbers]

}
