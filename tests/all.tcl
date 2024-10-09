# cookit tests
#
# Copyright (C) 2024 Konstantin Kushnir <chpock@gmail.com>
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.

package require Tcl 8.6-
source [file join [file dirname [info script]] common.tcl]
exit [runAllTests]
