# (c) 2024 Konstantin Kushnir

rename tcltest::test tcltest::test_memdebug

proc current_mem {} {
    lindex [split [lindex [lmap x [split [memory info] "\n"] { if { ![string match {current bytes allocated*} $x] } { continue } { set x } }] 0] " "] end
}

proc tcltest::test { args } {
    set id [lindex $args 0]

    set mem [current_mem]

    catch {
        memory tag $id
        uplevel 1 [list tcltest::test_memdebug {*}$args]
        memory tag ""
    } res opts

    puts "$id leak [expr { [current_mem] - $mem }]"

    catch { file delete -force memdump }
    memory active memdump
    set fi [open memdump r]
    set fo [open "${id}.dump" w]
    while { [gets $fi line] != -1 } {
        if { [string match "* $id" $line] } {
            puts $fo $line
        }
    }
    close $fi
    close $fo
    file delete -force memdump

}