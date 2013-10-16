# pkgIndex.tcl for mapscript
#
# load shared library, and source supplementaly ref procs

package ifneeded Mapscript 1.3 " 
    load [list [file join $dir libMapscript13[info sharedlibextension]]]
    # source the object reference helpers and convenience procs
    source [list [file join $dir mapscriptsupp.tcl]]
    package provide Mapscript 1.3
"


