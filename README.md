# cookfs

## About

This repository is an attempt to revive this amazing project. Original source repository is at [sourceforge.net](https://sourceforge.net/projects/cookit/files/cookfs/) and its homepage is at [http://www.endorser.org/en/blog/tcl/cookfs](http://www.endorser.org/en/blog/tcl/cookfs). As for now, the homepage is unmaintained and is only available via [WebArchive](http://web.archive.org/web/20150619084055/http://www.endorser.org/en/blog/tcl/cookfs).

Cookfs is a Tcl virtual filesystem using a compressed archive format to allow embedding multiple files in an archive that Tcl scripts can access directly.

It is optimized for storing Tcl packages (allowing around 10%-20% smaller sizes ratio than mk4vfs while still using zlib compression), small, fast and integrated with Tcl.

It is designed only for use as Tcl VFS and provides multiple optimizations especially for delivering Tcl based standalone applications.

## Compatibility

This package requires Tcl 8.5 or later, however it has only been tested with Tcl 8.6.14, 8.6.15 and Tcl 9.0.0.

Tcl variant of Cookfs also requires the [tclvfs](https://core.tcl-lang.org/tclvfs/index) package.

## Built packages and sources

The source code is available on [Github](https://github.com/chpock/cookfs).

This package uses the Tcl Extension Architecture (TEA) to build and install on Linux, Mac, or Windows platforms. For Windows platform only building with Mingw-w64 toolchain is supported.

The standard TEA config, make and install process is supported.

```
$ git clone https://github.com/chpock/cookfs.git
$ cd cookfs
$ git submodule update --init --recursive
$ mkdir build && cd build
$ ../configure
$ make
$ make test
$ make install
```

Already built packages are available on Github under [Releases](https://github.com/chpock/cookfs/releases).

There are packages for the following platforms:

- **Windows x86** and **x86\_64**: Windows XP or higher is required. However, they are only tested on Windows 10.
- **Linux x86** and **x86\_64**: built and tested on Cenos6.10. Require glibc v2.12 or higher.
- **MacOS x86** and **x86\_64**: built and tested on MacOS 10.13. However, these packages should be compatible with MacOS as of version 10.10.

## Copyrights

Copyright (c) 2010-2011 Wojciech Kocjan, Pawel Salawa

Copyright (c) 2024 Konstantin Kushnir <chpock@gmail.com>

## License

This code is licensed under the same terms as the Tcl Core.

This package contains bzip2 source code, which is copyright Julian R Seward.

License for bzip2 is contained in file "bzip2/LICENSE" and applies to all
files under bzip2/ subdirectory.

This package contains LZMA SDK, which is copyright Igor Pavlov.

License for LZMA SDK is contained in file "7zip/DOC/License.txt" and applies to all
files under 7zip/ subdirectory.

This package contains Zstandard source code, which is copyright Meta Platforms, Inc. and affiliates.

License for Zstandard is contained in file "zstd/LICENSE" and applies to all
files under zstd/ subdirectory.

This package contains brotli source code, which is copyright the Brotli Authors.

License for brotli is contained in file "brotli/LICENSE" and applies to all
files under brotli/ subdirectory.
