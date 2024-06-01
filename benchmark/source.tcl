# cookfs benchmark
#
# Copyright (C) 2024 Konstantin Kushnir <chpock@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

set archives {
    "Linux kernel" {
        url  "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/snapshot/linux-6.9-rc1.tar.gz"
        url_type "tar.gz"
        hash 259960416
    }
    "OpenSearch" {
        url "https://artifacts.opensearch.org/releases/bundle/opensearch/2.12.0/opensearch-2.12.0-linux-x64.tar.gz"
        url_type "tar.gz"
        hash "2821482878"
    }
    "Firefox" {
        url "https://download-installer.cdn.mozilla.net/pub/firefox/releases/124.0.1/linux-x86_64/en-US/firefox-124.0.1.tar.bz2"
        url_type "tar.bz2"
        hash "3892190051"
    }
    "Chromium" {
        url "https://www.googleapis.com/download/storage/v1/b/chromium-browser-snapshots/o/Linux_x64%2F1282381%2Fchrome-linux.zip?alt=media"
        url_type "zip"
        hash "126861572"
    }
    "Perl" {
        url "https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/download/SP_53822_64bit/strawberry-perl-5.38.2.2-64bit-PDL.zip"
        url_type "zip"
        hash "1222040341"
    }
    "Tcllib" {
        url "https://core.tcl-lang.org/tcllib/uv/tcllib-1.21.tar.gz"
        url_type "tar.gz"
        hash "3992597495"
    }
}

set cases {
    "gzip"       { cmd {tar -Pczf @d @s}              }
    "gzip:-9"    { cmd {tar -I "gzip -9"  -Pcf @d @s} }
    "bzip2"      { cmd {tar -Pcjf @d @s}              }
    "xz"         { cmd {tar -PcJf @d @s}              }
    "xz:-9"      { cmd {tar -I "xz -9"    -Pcf @d @s} }
    "xz:-9e"     { cmd {tar -I "xz -9e"   -Pcf @d @s} }
    "zstd"       { cmd {tar --zstd        -Pcf @d @s} }
    "zstd:-12"   { cmd {tar -I "zstd -12" -Pcf @d @s} }
    "zstd:-19"   { cmd {tar -I "zstd -19" -Pcf @d @s} }
    "7z:ppmd"    { cmd {7z a -m0=ppmd:mem=1024m:o=10 @d. @s} }
}

set recipes {
    "default" { args {} }
    "zlib"    { args {-compression zlib} }
    "lzma"    { args {-compression lzma}   }
}