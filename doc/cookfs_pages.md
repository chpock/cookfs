
[//000000001]: # (cookfs::pages \- cookfs::pages)
[//000000002]: # (Generated from file 'cookfs\_pages\.man' by tcllib/doctools with format 'markdown')
[//000000003]: # (Copyright &copy; 2010\-2011 Wojciech Kocjan <wojciech@kocjan\.org>)
[//000000004]: # (Copyright &copy; 2024 Konstantin Kushnir <chpock@gmail\.com>)
[//000000005]: # (cookfs::pages\(n\) 1\.9\.0 cookfs\_pages "cookfs::pages")

# NAME

cookfs::pages \- cookfs::pages

# <a name='toc'></a>Table Of Contents

  - [Table Of Contents](#toc)

  - [Synopsis](#synopsis)

  - [Description](#section1)

  - [COMMANDS](#section2)

  - [PAGES OPTIONS](#section3)

  - [See Also](#seealso)

  - [Keywords](#keywords)

  - [Copyright](#copyright)

# <a name='synopsis'></a>SYNOPSIS

package require Tcl 8\.5  
package require cookfs ?1\.9\.0?  

[__::cookfs::pages__ ?*options*? *archive*](#1)  
[*pagesHandle* __aside__ *filename*](#2)  
[*pagesHandle* __add__ *contents*](#3)  
[*pagesHandle* __get__ ?*\-weight weight*? *pageIdx*](#4)  
[*pagesHandle* __length__](#5)  
[*pagesHandle* __index__ ?*newIndex*?](#6)  
[*pagesHandle* __dataoffset__](#7)  
[*pagesHandle* __delete__](#8)  
[*pagesHandle* __gethead__](#9)  
[*pagesHandle* __getheadmd5__](#10)  
[*pagesHandle* __gettail__](#11)  
[*pagesHandle* __gettailmd5__](#12)  
[*pagesHandle* __cachesize__ ?*numPages*?](#13)  
[*pagesHandle* __getcache__ ?*pageIdx*?](#14)  
[*pagesHandle* __ticktock__ ?*maxAge*?](#15)  
[*pagesHandle* __filesize__](#16)  
[*pagesHandle* __hash__ ?*hashname*?](#17)  
[*pagesHandle* __close__](#18)  
[*pagesHandle* __password__ *secret*](#19)  

# <a name='description'></a>DESCRIPTION

Cookfs pages provide a low level mechanism for reading and writing to cookfs
pages\.

This mechanism should only be used by advanced users and is only documented for
reference\.

# <a name='section2'></a>COMMANDS

  - <a name='1'></a>__::cookfs::pages__ ?*options*? *archive*

    Creates a new pages object that can be used to read and write pages\.

    The command returns a pages command that can be used to perform actions on
    pages\.

    For definition of available options, see [PAGES OPTIONS](#section3)\.

  - <a name='2'></a>*pagesHandle* __aside__ *filename*

    Uses a separate file for storing changes to an archive\. This can be used to
    keep changes for a read\-only archive in a separate file\. See __cookfs__
    for more details on aside files and how they can be used\.

  - <a name='3'></a>*pagesHandle* __add__ *contents*

    Add a new page to archive\. Index of new page is returned\. Index will be
    between 0 and __length__\-1\.

    When adding a page that already exists, index to already existing page is
    returned\. Otherwise a new page is added at the end and its index is
    returned\.

  - <a name='4'></a>*pagesHandle* __get__ ?*\-weight weight*? *pageIdx*

    Gets a page in an archive\. This can be between 0 and __length__\-1\.

    If *\-weight weight* is specified, the appropriate weight will be set for
    the cache entry for the returned page\. If it is not specified, the weight of
    the cache entry for the returned page will be set to 0\.

    See __cookfs__ for more details on caching in cookfs\.

  - <a name='5'></a>*pagesHandle* __length__

    Returns number of pages in an archive\.

  - <a name='6'></a>*pagesHandle* __index__ ?*newIndex*?

    Get or set index data\. Index is a binary data that is stored in an archive
    and can be changed at any time\. This is used to store exported version of
    __cookfs\_fsindex__\.

  - <a name='7'></a>*pagesHandle* __dataoffset__

    Returns offset to beginning of cookfs archive in the file\.

  - <a name='8'></a>*pagesHandle* __delete__

    Saves all changes and deletes a cookfs pages object\.

  - <a name='9'></a>*pagesHandle* __gethead__

  - <a name='10'></a>*pagesHandle* __getheadmd5__

  - <a name='11'></a>*pagesHandle* __gettail__

  - <a name='12'></a>*pagesHandle* __gettailmd5__

    Gets part of file as binary data or MD5 checksum of it\. Head is the part of
    the file prior to cookfs archive\. Tail is the entire cookfs archive\.

    These commands can be used as helper procedures to read or compare currently
    open/mounted binaries\.

  - <a name='13'></a>*pagesHandle* __cachesize__ ?*numPages*?

    Sets or gets maximum number of pages to store in cache\. If *numPages* is
    specified, size is modified\. If more than *numPages* pages are currently
    buffered, they are removed from cache\. If 0, no cache is used\. Otherwise, up
    to *numPages* are kept in memory

  - <a name='14'></a>*pagesHandle* __getcache__ ?*pageIdx*?

    If *pageIdx* is specified, returns a boolean value corresponding to
    whether the internal cache contains data for the specified page\.

    If *pageIdx* is not specified, the content of the cache is returned\.

    The contents of the cache are a list of entries\. Each element in the list is
    a Tcl dict with keys: index, weight and age\.

    This method is mainly used in tests\.

  - <a name='15'></a>*pagesHandle* __ticktock__ ?*maxAge*?

    If *maxAge* is specified, sets the maximum age for cache entries\.

    It *maxAge* is not specified, the age of all cache entries is incremented
    by 1\. The current age for each record is also checked against the previously
    set maxAge\. If the current age of an entry is greater than or equal to the
    maxAge value set previously, its weight will be reset to 0\.

    See __cookfs__ for more details on caching in cookfs\.

  - <a name='16'></a>*pagesHandle* __filesize__

    Returns size of file up to last stored page\. The size only includes page
    sizes and does not include overhead for index and additional information
    used by cookfs\.

  - <a name='17'></a>*pagesHandle* __hash__ ?*hashname*?

    Hash function to use for comparing if pages are equal\. This is mainly used
    as pre\-check and entire page is still checked for\. Defaults to __md5__,
    can also be __crc32__\. Mainly for internal/testing at this moment\. Do
    not use\.

  - <a name='18'></a>*pagesHandle* __close__

    Closes pages object and return offset to end of cookfs archive\. This is
    almost an equivalent of calling __delete__\.

    This is meant for internal development and should not be used outside
    cookfs\.

  - <a name='19'></a>*pagesHandle* __password__ *secret*

    Specifies the password to be used for encryption\. Empty *secret* disables
    encryption for the following added files\.

    If aside changes feature is active for the current pages, this command will
    only affect the corresponding mounted aside archive\.

    See __cookfs__ for more details on encryption in cookfs\.

# <a name='section3'></a>PAGES OPTIONS

The following options can be specified when creating a cookfs pages object:

  - __\-readwrite__

    Open as read\-write, which is the default\. If file exists and is valid cookfs
    pages format archive, it is used\. Otherwise new cookfs pages archive is
    created at end of file\.

  - __\-readonly__

    Open as read\-only\. File must exist and be in valid cookfs pages format
    archive, otherwise operation will fail\.

  - __\-cachesize__ *numPages*

    Maximum number of pages to store in cache\. If 0, no cache is used\.
    Otherwise, up to *numPages* are kept in memory for efficient access\.

  - __\-endoffset__ *offset*

    Read archive starting at specified offset, instead of reading from end of
    file\.

    This feature is used when cookfs archive is not stored at end of file \- for
    example if it is used in tclkit and followed by mk4vfs archive\.

  - __\-compression__ *none&#124;zlib&#124;bz2&#124;lzma&#124;zstd&#124;brotli&#124;custom*

    Compression to use for storing new files\.

    See __cookfs__ for more details on compression in cookfs\.

  - __\-compresscommand__ *tcl command*

    For *custom* compression, specifies command to use for compressing pages\.

    See __cookfs__ for more details on compression in cookfs\.

  - __\-decompresscommand__ *tcl command*

    For *custom* compression, specifies command to use for decompressing
    pages\.

    See __cookfs__ for more details on compression in cookfs\.

  - __\-encryptkey__

    Enables key\-based encryption\.

    See __cookfs__ for more details on encryption in cookfs\.

  - __\-encryptlevel__ *level*

    Specifies the encryption level, which must be an unsigned integer from 0 to
    31 inclusive\. The default encryption level is 15\.

    See __cookfs__ for more details on encryption in cookfs\.

  - __\-password__ *secret*

    Specifies the password to be used for encryption\. The *secret* argument
    must not be an empty string\.

    See __cookfs__ for more details on encryption in cookfs\.

# <a name='seealso'></a>SEE ALSO

cookfs\_fsindex, cookfs\_pages

# <a name='keywords'></a>KEYWORDS

cookfs, vfs

# <a name='copyright'></a>COPYRIGHT

Copyright &copy; 2010\-2011 Wojciech Kocjan <wojciech@kocjan\.org>  
Copyright &copy; 2024 Konstantin Kushnir <chpock@gmail\.com>
