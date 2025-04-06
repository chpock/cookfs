
[//000000001]: # (cookfs \- cookfs)
[//000000002]: # (Generated from file 'cookfs\.man' by tcllib/doctools with format 'markdown')
[//000000003]: # (Copyright &copy; 2010\-2011 Wojciech Kocjan <wojciech@kocjan\.org>)
[//000000004]: # (Copyright &copy; 2024 Konstantin Kushnir <chpock@gmail\.com>)
[//000000005]: # (cookfs\(n\) 1\.9\.0 cookfs "cookfs")

# NAME

cookfs \- cookfs

# <a name='toc'></a>Table Of Contents

  - [Table Of Contents](#toc)

  - [Synopsis](#synopsis)

  - [Description](#section1)

  - [COMMANDS](#section2)

  - [MOUNT OPTIONS](#section3)

  - [COOKFS STORAGE](#section4)

  - [COMPRESSSION](#section5)

  - [COOKFS METADATA](#section6)

  - [ASIDE AND WRITE TO MEMORY](#section7)

  - [CACHING](#section8)

  - [MULTI\-THREADING SUPPORT](#section9)

  - [ENCRYPTION](#section10)

  - [ENCRYPTION \- FILE\-BASED](#section11)

  - [ENCRYPTION \- KEY\-BASED](#section12)

  - [ENCRYPTION STRENGTH](#section13)

  - [FILESETS](#section14)

  - [See Also](#seealso)

  - [Keywords](#keywords)

  - [Copyright](#copyright)

# <a name='synopsis'></a>SYNOPSIS

package require Tcl 8\.5  
package require cookfs ?1\.9\.0?  

[__::cookfs::Mount__ ?*options*? *archive* *local* ?*options*?](#1)  
[__::cookfs::Mount__ ?*options*? *\-writetomemory* *local* ?*options*?](#2)  
[__::vfs::cookfs::Mount__ ?*options*? *archive* *local* ?*options*?](#3)  
[__::cookfs::Unmount__ __fsid__](#4)  
[__::cookfs::Unmount__ __local__](#5)  
[*cookfsHandle* __aside__ *filename*](#6)  
[*cookfsHandle* __writetomemory__](#7)  
[*cookfsHandle* __optimizelist__ *base* *filelist*](#8)  
[*cookfsHandle* __getmetadata__ *parameterName* ?*defaultValue*?](#9)  
[*cookfsHandle* __setmetadata__ *parameterName* *value*](#10)  
[*cookfsHandle* __writeFiles__ ?*filename1* *type1* *data1* *size1* ?*filename2* *type2* *data2* *size2* ?*\.\.*???](#11)  
[*cookfsHandle* __filesize__](#12)  
[*cookfsHandle* __smallfilebuffersize__](#13)  
[*cookfsHandle* __password__ *secret*](#14)  

# <a name='description'></a>DESCRIPTION

Package __cookfs__ is a Tcl virtual filesystem \(VFS\) that allows storing one
or more files in a single file\. This is similar to mk4vfs, zipvfs and many other
archive formats\.

The main difference is that cookfs was designed for Tcl\-related use and is
optimized for both shipping Tcl scripts as well as delivering large payloads in
an efficient way\.

# <a name='section2'></a>COMMANDS

  - <a name='1'></a>__::cookfs::Mount__ ?*options*? *archive* *local* ?*options*?

  - <a name='2'></a>__::cookfs::Mount__ ?*options*? *\-writetomemory* *local* ?*options*?

  - <a name='3'></a>__::vfs::cookfs::Mount__ ?*options*? *archive* *local* ?*options*?

    Mounts cookfs archive\. Archive is read from *archive* and can later be
    accessed from Tcl using *local* as mount point\. Options can preceed or
    succeed *archive* and *local* arguments\.

    If *\-writetomemory* argument is specified, argument *archive* may be
    omitted\. This will create a fully in\-memory vfs at the specified mount
    point\.

    The command returns a cookfs handle which is also a command that can be used
    to perform certain actions on cookfs archive\.

    For definition of available options, see [MOUNT OPTIONS](#section3)\.

  - <a name='4'></a>__::cookfs::Unmount__ __fsid__

  - <a name='5'></a>__::cookfs::Unmount__ __local__

    Unmounts a cookfs archive\.

    The __vfs::unmount__ command can also be used to unmount archives, but
    it is only available if the tclvfs package was loaded at mount time\. Thus,
    it is not recommended to unmount archives using the __vfs::unmount__
    command, and this feature is only present for backward compatibility\.

    For cookfs archives it is very important to properly unmount them after
    performing all operations as many changes are only stored on unmount
    operation\.

  - <a name='6'></a>*cookfsHandle* __aside__ *filename*

    Uses a separate file for storing changes to an archive\. This can be used to
    keep changes for a read\-only archive in a separate file\.

    See [ASIDE AND WRITE TO MEMORY](#section7) for more details on aside
    files and how they can be used\.

  - <a name='7'></a>*cookfsHandle* __writetomemory__

    Stores all further changes to this archive only in memory\. This can be used
    to store temporary data or applying changes that do not persist across
    cookfs filesystem remounts and/or application restarts\.

    See [ASIDE AND WRITE TO MEMORY](#section7) for more details on write to
    memory feature\.

  - <a name='8'></a>*cookfsHandle* __optimizelist__ *base* *filelist*

    Takes a list of files and optimizes them to reduce number of pages read by
    cookfs\. This is mainly useful when unpacking very large number of files\.

    Parameter *base* specifies path to be prepended when getting file
    information\. When list of files is relative to archive root, *base* should
    be empty\. When *filelist* contains paths relative to subdirectory of
    archive root, *base* should specify path to this subdirectory\.

    For example if archive contains files __contents/dir1/file1__ and
    __contents/dir1/subdir/file2__, the following code can be used:
    Specifying *filelist* relative to a directory and proper *base*:

        % puts [$fsid optimizelist contents/dir1 {file1 subdir/file2}]
        subdir/file2
        file1

    Specifying *filelist* relative to root directory:

        % puts [$fsid optimizelist "" {contents/dir1/file1 contents/dir1/subdir/file2}]
        contents/dir1/subdir/file2
        contents/dir1/file1

  - <a name='9'></a>*cookfsHandle* __getmetadata__ *parameterName* ?*defaultValue*?

    Gets a parameter from cookfs metadata\. If *parameterName* is currently set
    in metadata, value for it is returned\.

    If *parameterName* is not currently set, *defaultValue* is returned if
    it was specified\. If *defaultValue* was not specified and parameter is not
    set, an error is thrown\.

    See [COOKFS METADATA](#section6) for more details on metadata storage
    in cookfs archives\.

  - <a name='10'></a>*cookfsHandle* __setmetadata__ *parameterName* *value*

    Set *parameterName* to specified *value*\. If parameter currently exists,
    it is overwritten\.

    See [COOKFS METADATA](#section6) for more details on metadata storage
    in cookfs archives\.

  - <a name='11'></a>*cookfsHandle* __writeFiles__ ?*filename1* *type1* *data1* *size1* ?*filename2* *type2* *data2* *size2* ?*\.\.*???

    Write one or more files to cookfs archive\. Specified as list of one or more
    4\-element entries describing files\. Each *filename* specifies name of the
    file, relative to archive root\. Elements *type* and *data* specify
    source for adding a file as well as actual data\. *Size* specifies size of
    the file\. If specified as empty string, it is calculated based on *type*\.

    The following values for *type* are accepted:

      * __file__ \- *data* specifies path to file that should be put in the
        cookfs archive; path is relative to current working directory, same as
        for command such as __file copy__; file is copied directly and does
        not require staging file in memory, unlike writing a file in cookfs
        archive using __open__

      * __data__ \- *data* is data to be stored in file as binary

      * __channel__ \- *data* is a valid Tcl channel that should be read by
        cookfs; channel is read from current location until end or until
        *size* bytes have been read

  - <a name='12'></a>*cookfsHandle* __filesize__

    Returns size of file up to last stored page\. The size only includes page
    sizes and does not include overhead for index and additional information
    used by cookfs\.

  - <a name='13'></a>*cookfsHandle* __smallfilebuffersize__

    Returns size of all files that are queued up to be written\.

  - <a name='14'></a>*cookfsHandle* __password__ *secret*

    Specifies the password to be used for encryption\. Empty *secret* disables
    encryption for the following added files\.

    If aside changes feature is active for the current VFS, this command will
    only affect the corresponding mounted aside archive\.

    See [ENCRYPTION](#section10) for more details on encryption in cookfs\.

# <a name='section3'></a>MOUNT OPTIONS

The following options can be specified when mounting a cookfs archive:

  - __\-readonly__

    Mount archive as read\-only\. Archive must exist and be a valid cookfs
    archive, otherwise mount will fail\.

  - __\-writetomemory__

    Enable write to memory feature for this mount\.

    See [ASIDE AND WRITE TO MEMORY](#section7) for more details on write to
    memory feature\.

  - __\-pagesize__ *bytes*

    Specifies maximum size of a page\. This specifies how much bytes a chunk
    \(storing entire file, part of a file or multiple files\) can be\. Setting
    larger values increases memory usage, but may increase compression ratio,
    especially for compressors such as bzip2 or LZMA, which compress larger
    chunks of information more efficiently\.

    See [COOKFS STORAGE](#section4) for more details on how cookfs stores
    files, index and metadata\.

  - __\-pagecachesize__ *numPages*

    Maximum number of pages to be stored in memory when reading data from cookfs
    archive\. If 0, no cache is used\. Otherwise, up to *numPages* are kept in
    memory for efficient access\. Increasing the value directly affects speed of
    reading operations, but also increases memory\.

    Maximum number of memory used for cache is number of pages to cache
    multiplied by maximum size of a page\.

    See [COOKFS STORAGE](#section4) for more details on how cookfs stores
    files, index and metadata\.

  - __\-smallfilesize__ *bytes*

    Specifies threshold for small files\. All files smaller than this value are
    treated as small files and are stored and compressed as multiple files for
    efficiency\.

    See [COOKFS STORAGE](#section4) for more details on how cookfs stores
    files, index and metadata\.

  - __\-smallfilebuffer__ *bytes*

    Specifies maximum buffer for small files\. Specifying larger value allows
    storing more files in memory before saving them on disk\. This can produce
    better compression ratio\.

    See [COOKFS STORAGE](#section4) for more details on how cookfs stores
    files, index and metadata\.

  - __\-volume__

    Register mount point as Tcl volume \- useful for creating mount points in
    locations that do not exist \- such as *archive://*\.

  - __\-compression__ *none&#124;zlib&#124;bz2&#124;lzma&#124;zstd&#124;brotli&#124;custom*?:*level*?

    Compression to use for storing new files\.

    See [COMPRESSSION](#section5) for more details on compression in
    cookfs\.

  - __\-compresscommand__ *tcl command*

    For *custom* compression, specifies command to use for compressing pages\.

    See [COMPRESSSION](#section5) for more details on compression in
    cookfs\.

  - __\-decompresscommand__ *tcl command*

    For *custom* compression, specifies command to use for decompressing
    pages\.

    See [COMPRESSSION](#section5) for more details on compression in
    cookfs\.

  - __\-endoffset__ *offset*

    Read archive starting at specified offset, instead of reading from end of
    file\.

    This feature is used when cookfs archive is not stored at end of file \- for
    example if it is used in tclkit and followed by mk4vfs archive\.

  - __\-setmetadata__ *list*

    Set specified metadata after mounting\. Convenience for setting metadata in
    mount command\. List should be a name\-value pairs list indicating parameter
    names and values to set\.

  - __\-noregister__

    Do not register this mount point with tclvfs\. Mainly for internal use\.

  - __\-nocommand__

    Do not register Tcl command for this cookfs handle\. Mainly for internal use\.

  - __\-bootstrap__ *tcl code*

    Bootstrap code for cookit binaries\. Mainly for internal use\.

  - __\-pagehash__ *hash*

    Hash function to use for comparing if pages are equal\. This is mainly used
    as pre\-check and entire page is still checked for\. Defaults to __md5__,
    can also be __crc32__\. Mainly for internal/testing at this moment\. Do
    not use\.

  - __\-fsindexobject__ *fsiagesObject*

    Do not create cookfs::fsindex object, use specified fsindex object\. Mainly
    for internal use\.

  - __\-pagesobject__ *pagesObject*

    Do not create cookfs::pages object, use specified pages object\. Mainly for
    internal use\.

  - __\-shared__

    Create a multi\-threaded cookfs that is available to all application threads\.

    See [MULTI\-THREADING SUPPORT](#section9) for more details on
    multi\-threading support in cookfs\.

  - __\-encryptkey__

    Enables key\-based encryption\.

    See [ENCRYPTION](#section10) for more details on encryption in cookfs\.

  - __\-encryptlevel__ *level*

    Specifies the encryption level, which must be an unsigned integer from 0 to
    31 inclusive\. The default encryption level is 15\.

    See [ENCRYPTION](#section10) for more details on encryption in cookfs\.

  - __\-password__ *secret*

    Specifies the password to be used for encryption\. The *secret* argument
    must not be an empty string\.

    See [ENCRYPTION](#section10) for more details on encryption in cookfs\.

  - __\-fileset__ *fileset*

    For a newly created archive specifies the mode for filesets, which can be
    *auto*, *platform* or *tcl\_version*\. Or it specifies a certain name
    for fileset\. The mode in this case will be *custom*\.

    When opening an already existing archive, it specifies the fileset that will
    be activated after opening\.

    See [FILESETS](#section14) for more details on filesets in cookfs\.

# <a name='section4'></a>COOKFS STORAGE

Cookfs uses __cookfs::pages__ for storing all files and directories in an
archive\. Information on which pages store what files is stored in
__cookfs::fsindex__ object, that is stored as part of archive\.

Cookfs stores files in one of the following ways:

  1. Entire file is stored in a single cookfs page\. This happens if file's size
     is larger than *\-smallfilesize*, but smaller than *\-pagesize*\.

  1. Entire file is stored in multiple cookfs pages\. This happens if file's size
     is larger than *\-smallfilesize*, and larger than *\-pagesize*\.

  1. Multiple files are stored in a single cookfs page\. This happens if file's
     size is smaller than *\-smallfilesize*\.

First and second case are similar and a limit on page size is mainly used to
allow faster seek operations and increate I/O operation speed\.

When compressing large files as a single stream \(such as in zipvfs and mk4vfs\),
seeking operation requires re\-reading and ignoring all data up to new location\.
With this approach, seek operations are instant since only new pages are read
and data prior to new seek location is ignored\.

Storing multiple files in a single page is used to increase compression ratio
for multiple small files \- such as Tcl scripts\. All small files \(whose size does
not exceed *\-smallfilesize*\) are grouped by their extension, followed by their
file name\. This allows files such as __pkgIndex\.tcl__ to be compressed in
same page, which is much more efficient than compressing each of them
independantly\.

# <a name='section5'></a>COMPRESSSION

Cookfs uses compression to store pages and filesystem index more efficiently\.
Pages are compressed as a whole and independant of files\. Filesystem index is
also compressed to reduce file size\.

Compression is specified as *\-compression* option when mounting an archive\.
This option applies to newly stored pages and whenever a file index should be
saved\. Existing pages are not re\-compressed on compression change\.

The *\-compression* option accepts a compression method and an optional
compression level, separated by a colon character \(__:__\)\. The compression
level must be an integer in the range of \-1 to 255\. Different compression
methods define the compression level differently\. However, it is safe to specify
a compression level that is lower or higher than what is supported by a
particular method\. In these cases, the minimum or maximum level will be used
according to the specific compression method\. This means that it is possible and
safe to specify level 255 for any method, and the maximum level will be used\.

If no compression level is specified, or it is an empty string or \-1, then the
default level value specific to the chosen compression method will be used\.

For example:

    # This will mount an archive with the zlib compression method and with default level (6)
    cookfs::Mount archive archive -compression zlib

    # This will mount an archive with the lzma compression method and with compression level 3
    cookfs::Mount archive archive -compression lzma:3

    # This will mount an archive with the bzip2 compression method and with the maximum compression level for that method (9)
    cookfs::Mount archive archive -compression bz2:255

Cookfs provides the following compressions:

  - __none__ \- no compression is used at all; mainly used for testing
    purposes or when content should not be packed at all\. The compression level
    is ignored in this case\.

  - __zlib__ \- use zlib compression to compress \- same as with mk4vfs and
    zipvfs\. Compression levels can be from 1 to 9\. The default compression level
    is 6\.

  - __bz2__ \- use bzip2 for compression; requires specifying
    __\-\-enable\-bz2__ when building cookfs\. Compression levels can be from 1
    to 9\. The default compression level is 9\. The compression level for this
    method means the block size and is counted as the level multiplied by 100k
    bytes\. The default compression level of 9 corresponds to a block size of 900
    KB\.

  - __lzma__ \- use lzma for compression; requires specifying
    __\-\-enable\-lzma__ when building cookfs\. Compression levels can be from 0
    to 9\. The default compression level is 5\.

  - __zstd__ \- use Zstandard for compression; requires specifying
    __\-\-enable\-zstd__ when building cookfs\. Compression levels can be from 1
    to 22\. The default compression level is 3\.

  - __brotli__ \- use brotli for compression; requires specifying
    __\-\-enable\-brotli__ when building cookfs\. Compression levels can be from
    0 to 22\. The default compression level is 6\.

  - __custom__ \- use Tcl commands for compressing and decompressing pages\.
    The compression level is ignored in this case\.

For __custom__ compression, *\-compresscommand* and *\-decompresscommand*
have to be specified\. These commands will be used to compress data and will be
called with data to be compressed or decompressed as additional argument\.

For example the following are sample compress and decompress commands:

    # use vfs::zip to (de)compress data and encode it in HEX
    proc testc {d} {
        binary scan [vfs::zip -mode compress $d] H* rc
        return "HEXTEST-$rc"
    }
    proc testd {d} {
        set rc [vfs::zip -mode decompress [binary format H* [string range $d 8 end]]]
        return $rc
    }

    cookfs::Mount archive archive -compression custom -compresscommand testc -decompresscommand testd

See [COOKFS STORAGE](#section4) for more details on how files are stored in
cookfs\.

Default values for *\-pagesize*, *\-smallfilesize* and *\-smallfilebuffer*
parameters provided by cookfs have been chosen carefully\. These are outcome of
performing multiple tests\. It is not recommended to change it for __zlib__
compression\.

# <a name='section6'></a>COOKFS METADATA

Cookfs supports storing metadata in an archive\. Metadata are any number of
properties stored in a cookfs archive\.

Cookfs does not impose any limits or rules on naming of properties as well as
possible values, but it is recommended that cookfs internal properties are
stored using __cookfs\.\*__ prefix and cookit related properties use
__cookit\.\*__ format\.

User properties do not need to use any prefix or suffix, but it is recommended
to use form of __*appname*\.\*__\.

# <a name='section7'></a>ASIDE AND WRITE TO MEMORY

Cookfs supports a feature called aside changes\. It stores all changes in a
separate file\. It can be used for updating files in read\-only archives such as
standalone binaries or data on read\-only media\.

Aside functionality can be enabled for read\-only and read\-write archives\. All
types of changes can be performed and they will be stored in the new file\.
Operations such as deletions from a file are also possible\.

Aside changes are persistent\. It is possible to mount an archive, setup an aside
file and perform changes and unmount it\. When remounting the archive, changes
will not be visible\. However, after enabling aside and pointing to same file as
before, changes will instantly be visible\.

Aside changes do not cause increased use of memory \- changes are written to disk
in same fashion, using new file instead of the original one\.

Write to memory feature is slightly different\. It allows read\-only and
read\-write archives to store all changes in memory and those will not be saved
back on disk\. This feature is mainly intended for handling temporary files or
running legacy code that requires write access to files inside a read\-only
archive\.

# <a name='section8'></a>CACHING

In order to make I/O operations with pages more efficient and avoid additional
page read attempts, a page cache is used\.

Generally, pages that contain more than one file are more interesting to cache\.
For these pages, it is more likely that they will be needed to read the
following files\.

To provide higher priority caching for pages with many files, each cache entry
has a "weight" field\. The value of this field is 0 for pages that contain data
from only one file\. And the value of this field is 1 if this page contains data
from multiple files\. According to this field, the priority for storing pages in
the cache will be implemented\. When replacing pages in the cache, the page with
the minimum weight is replaced first\.

However, another issue arises with this approach\. There may be a situation where
a page with several files will be stored in the cache, but access to these files
is no longer needed\. In this case, the cached page will reside in the cache and
prevent more used pages from being added\.

To solve this issue, each cache entry also has an "age" field\. The "age" field
is a special value that is incremented by 1 during page operations\. When this
value reaches a certain limit, the page weight is reset to the default value of
0\. Also, if the page is requested from the cache and used, the value of its
"age" will be reset to the original value 0\.

This makes it possible to free up cache space from prioritized but unused pages\.

The concept of "age" has the following nuances:

It is possible to read a file splitted into a large number of pages\. To avoid
adding "age" multiple times for all cache elements when reading each page, "age"
for all items will be incremented only on the very first attempt to read this
file\.

The opposite situation is possible, when a single page contains a large number
of small files\. To avoid "age" overflow for other pages when reading these
files, the field "age" will only increase the first time this page is read\. If
this page already exists in the cache, then the field "age" for cache items will
not be increased\.

# <a name='section9'></a>MULTI\-THREADING SUPPORT

By default, cookfs are created that are only available within a single
application thread, but to all interpreters\.

Using the *\-shared* parameter, you can create a cookfs that will be available
to all application threads\.

The following nuances must be taken into account when working in multi\-threaded
mode:

1\. cookfs remains bound to the thread and interpreter in which it was created\.
This means that cookfs can only be unmounted in its own thread\. Also, when the
bound thread and/or interpreter is terminated, the cookfs will be unmounted and
become unavailable to all other threads\.

2\. *cookfsHandle* and internal __cookfs::pages__/__cookfs::fsindex__
objects are only available in bound thread\. Other threads can only use standard
Tcl commands like __file__, __open__, etc to access and manipulate files
in cookfs\.

3\. Tcl callbacks are not available for multi\-threaded cookfs\. In particular, it
means that compression type __custom__ is not available in this mode\. The
following list of options is not available for multi\-threaded cookfs:

  - *\-compresscommand*

  - *\-decompresscommand*

  - *\-asynccompresscommand*

  - *\-asyncdecompresscommand*

4\. The multi\-thread cookfs is only available when package is built with
__c\-vfs__ option enabled\.

# <a name='section10'></a>ENCRYPTION

Cookfs uses strong AES\-256\-CBC encryption with PBKDF2\-HMAC\-SHA256 key
derivation\. Encryption strength \(resistance to password brute force\) can be
configured as described in the [ENCRYPTION STRENGTH](#section13) section\.

Cookfs supports 2 encryption modes:

  - file\-based encryption

  - key\-based encryption

By default, file\-based encryption is used\. More information about this mode is
available in section [ENCRYPTION \- FILE\-BASED](#section11)\.

Key\-based encryption is quite different from file\-based encryption\. More details
about this mode can be found in section [ENCRYPTION \-
KEY\-BASED](#section12)\.

The encryption mode is set once when creating an archive and cannot be changed\.

# <a name='section11'></a>ENCRYPTION \- FILE\-BASED

File\-based encryption mode is the simplest mode and will be active by default\.
In this mode, files are directly encrypted using the specified password\. This
means that it is possible to use different passwords for different files\.

To disable encryption it is possible to set an empty password\. After that, new
added files will be unencrypted and there will be no access to already encrypted
files in cookfs\.

For example, this use case creates an archive with unprotected Tcl scripts\.
These scripts will ask the user for a password and then read other files using
the specified password\.

    # This code prepares the archive.
    set fsid [cookfs::Mount "archive" "/mount_point"]

    # Copy the necessary runtime scripts
    file copy ./my_scripts [file join "/mount_point" "scripts"]

    # Set the first password password
    $fsid password "my-secret-1"

    # Add files that will be protected by password "my-secret-1"
    file copy ./data1 [file join "/mount_point" "data1"]

    # Set another password
    $fsid password "my-secret-2"

    # Add files that will be protected by password "my-secret-2"
    file copy ./data2 [file join "/mount_point" "data2"]

    # Unmount and save the archive
    cookfs::Unmount "/mount_point"

This code uses the archive\.

    # First mount the archive without a password
    set fsid [cookfs::Mount "archive" "/mount_point" -readonly]
    # Execute our scripts, which are unencrypted and stored in "scripts" directory
    source [file join "/mount_point" "scripts" "main.tcl"]
    # Obtain the password or ask the user to provide one
    set secret_password [get_password_from_user]
    # Set the password as the current password for accessing files in cookfs
    $fsid password $secret_password
    # Now we can read/use the files in the "data1" or "data2" directory
    # if the provided password matches the files in these locations

If the password is set from the beginning when mounting cookfs, then all added
files will be encrypted\. However, it is possible to disable encryption by
setting an empty password and then add files that will not be protected by
encryption\.

To summarize, file\-base encryption has these advantages:

  - different files may be protected by different passwords

  - it is possible to open an archive without a password to read unprotected
    files and set a password later when you need to access protected files

On the other hand, there are the following disadvantages:

  - it is impossible to protect information about the file system \(file names,
    sizes, etc\.\)

  - it is impossible to change the encryption password for a file, it is only
    required to recreate the file with a new password

# <a name='section12'></a>ENCRYPTION \- KEY\-BASED

In this mode, files are encrypted with a random key\. In turn, this random key is
encrypted with the specified password and stored in the archive\. When opening
the archive, the random key will be decrypted with the specified password and
will be used to decrypt all files in the archive\.

The main advantage of this method is that it is possible to distribute the same
archive but with different passwords for different users/cases\. For this purpose
it is not necessary to create a new archive with a new password each time\. It is
possible to create a base archive with a known secret password\. When it is
necessary to transfer this archive with the required password, you can simply
change the password with which the encryption key is encrypted\. All other data
will remain unchanged\. For cases where it is necessary to use the same archive
but with different passwords, this method can save a lot of time in creating an
archive for each case, and also improves reproducibility because all users will
have exactly the same set of files\.

This method has 2 variants when the information about the file system \(file
names, sizes, etc\.\) is encrypted and the archive cannot be opened without
specifying the correct password, and when the information about the file system
is not encrypted\.

To use this mode it is necessary to specify the switch __\-encryptkey__ at
the first mounting of cookfs\. Information about key\-based encryption mode will
be saved in the archive and further opening of the archive will also use this
mode\. It is impossible to change the mode in an already created archive\.

If both options __\-encryptkey__ and __\-password__ are used when mounting
the archive for the first time, then key\-based encryption mode with encryption
of information about the file system \(file names, sizes, etc\.\) will be set\. In
this case, it is necessary to specify the password when opening the file next
time\.

If at the first mounting of the archive only switch __\-encryptkey__ is used
without option __\-password__, then the encryption mode will be set without
encrypting information about the file system\. In this case it will be possible
to open the archive without specifying a password and use unencrypted data in
it\.

In this mode, the __password__ method works by setting or changing the
password for the protected key\.

For example, this use case creates an archive with unprotected Tcl scripts\.
These scripts will ask the user for a password and then read other files using
the specified password\.

    # This code prepares the archive.
    # First, we will create an archive with -encryptkey and without
    # the -password option, which will allow us to further open
    # the archive without a password
    set fsid [cookfs::Mount "archive" "/mount_point" -encryptkey]

    # Copy the necessary runtime scripts
    file copy ./my_scripts [file join "/mount_point" "scripts"]

    # Set the password password
    $fsid password "my-secret-1"

    # Add files that will be protected by password "my-secret-1"
    file copy ./data1 [file join "/mount_point" "data1"]

    # Unmount and save the archive
    cookfs::Unmount "/mount_point"

This code uses the archive:

    # First mount the archive without a password
    set fsid [cookfs::Mount "archive" "/mount_point"]

    # Execute our scripts, which are unencrypted and stored in "scripts" directory
    source [file join "/mount_point" "scripts" "main.tcl"]

    # Obtain the password or ask the user to provide one
    set secret_password [get_password_from_user]

    # Set the password as the current password for accessing files in cookfs
    $fsid password $secret_password

    # Now we can read/use the files in the "data1" directory
    # if the provided password is correct

This code changes the password for an already prepared archive:

    # First mount the archive
    set fsid [cookfs::Mount "archive" "/mount_point" -password "my-secret-1"]

    # Set the new password
    $fsid password "my-secret-2"

    # Unmount and save the archive
    cookfs::Unmount "/mount_point"

    # After that, to access the files in the "data1" directory from that archive,
    # it is necessary to use the password my-secret-2.

This example creates an archive with key\-base encryption method and with all
file system information \(file names, sizes, etc\.\) encrypted:

    # This code prepares the archive.
    set fsid [cookfs::Mount "archive" "/mount_point" -encryptkey -password "my-secret-1"]
    # Add files that will be protected by password "my-secret-1"
    file copy ./data1 [file join "/mount_point" "data1"]
    # Unmount and save the archive
    cookfs::Unmount "/mount_point"
    # Now this archive is fully encrypted. To open it, it is necessary to set
    # a password when mounting it.

To summarize, key\-base encryption has these advantages:

  - it is possible to easily change the password that should be used to open
    files, it is not necessary to completely recreate the archive

  - it is possible to open an archive without a password to read unprotected
    files and set a password later when you need to access protected files

On the other hand, there are the following disadvantages:

  - it is impossible to protect information about the file system \(file names,
    sizes, etc\.\)

  - it is impossible to have different passwords for different files, all files
    are encrypted with a single key

To summarize, key\-base encryption with encrypted file system information \(file
names, sizes, etc\.\) has these advantages:

  - it is possible to easily change the password that should be used to open
    files, it is not necessary to completely recreate the archive

  - all data about the contents of the archive is encrypted

On the other hand, there are the following disadvantages:

  - it is impossible to open the archive to read unprotected data, the password
    must be specified when mounting cookfs

  - it is impossible to have different passwords for different files, all files
    are encrypted with a single key

# <a name='section13'></a>ENCRYPTION STRENGTH

The encryption strength can be configured as the number of iterations for
PBKDF2\-HMAC\-SHA256, which can be set by parameter __\-encryptlevel__\.

This parameter must have a numeric value from 0 to 31 inclusive\. The number of
iterations for values from 0 to 15 are calculated as __\-encryptlevel__\*8192\.
The number of iterations for values 16 through 31 are calculated as
__\-encryptlevel__\*81920\. The default value is 15\.

Therefore, it is possible to set the number of iterations in the following
range:

  - minimum __\-encryptlevel__ value 0 \- number of iterations 0

  - default __\-encryptlevel__ value 15 \- number of iterations 122880

  - maximum __\-encryptlevel__ value 31 \- number of iterations 2539520

Note that the number of iterations affects the delay in setting or changing the
password\. The higher the number of iterations, the longer the delay will be\.

The encryption level is set when the archive is created\. It is saved in it and
this value will be automatically used for all further opening of the archive\.

# <a name='section14'></a>FILESETS

Cookfs allows the use of different sets of files in VFS and switching between
them\.

File sets can have either user\-defined names or automatic names depending on the
current platform or version of Tcl\.

For example, this allows to create a single archive that when mounted in Linux
and Tcl9\.0 environment will have one set of files, and when mounted in Windows
and Tcl8\.6 environment will have another set of files\.

The archive can be in 2 modes:

  - file sets are disabled and cannot be used \(this is the default mode\)

  - file sets are enabled

To enable the file set feature, it is necessary to use the __\-fileset__
option when initializing the archive\. This option accepts either one of the
values *auto*, *platform*, *tcl\_version*, or the name of the file set\.

The following modes are enabled:

  - *auto* value \- the active file set is automatically determined by the
    current Tcl version and platform

  - *platform* value \- the active file set is automatically determined by the
    current platform

  - *tcl\_version* value \- the active file set is automatically determined by
    the current Tcl version

  - if another value is specified \- this value will be used for the file set
    name

When mounting an archive in which the file set feature is enabled and the mode
is one of *auto*, *platform* or *tcl\_version*, then the active file set
will be selected automatically according to the current Tcl version and/or
platform\. If *custom* file set mode is set for the archive, then the first
available file set will be selected\.

# <a name='seealso'></a>SEE ALSO

cookfs\_fsindex, cookfs\_pages

# <a name='keywords'></a>KEYWORDS

cookfs, vfs

# <a name='copyright'></a>COPYRIGHT

Copyright &copy; 2010\-2011 Wojciech Kocjan <wojciech@kocjan\.org>  
Copyright &copy; 2024 Konstantin Kushnir <chpock@gmail\.com>
