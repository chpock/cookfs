
[//000000001]: # (cookfs::fsindex \- cookfs::fsindex)
[//000000002]: # (Generated from file 'cookfs\_fsindex\.man' by tcllib/doctools with format 'markdown')
[//000000003]: # (Copyright &copy; 2010\-2011 Wojciech Kocjan <wojciech@kocjan\.org>)
[//000000004]: # (Copyright &copy; 2024 Konstantin Kushnir <chpock@gmail\.com>)
[//000000005]: # (cookfs::fsindex\(n\) 1\.9\.0 cookfs\_fsindex "cookfs::fsindex")

# NAME

cookfs::fsindex \- cookfs::fsindex

# <a name='toc'></a>Table Of Contents

  - [Table Of Contents](#toc)

  - [Synopsis](#synopsis)

  - [Description](#section1)

  - [COMMANDS](#section2)

  - [BLOCK\-OFFSET\-SIZE INFORMATION](#section3)

  - [See Also](#seealso)

  - [Keywords](#keywords)

  - [Copyright](#copyright)

# <a name='synopsis'></a>SYNOPSIS

package require Tcl 8\.5  
package require cookfs ?1\.9\.0?  

[__::cookfs::fsindex__ ?*exportedData*?](#1)  
[*fsindexHandle* __export__](#2)  
[*fsindexHandle* __list__ *path*](#3)  
[*fsindexHandle* __get__ *path*](#4)  
[*fsindexHandle* __getmtime__ *path*](#5)  
[*fsindexHandle* __set__ *path* *mtime* ?*data*?](#6)  
[*fsindexHandle* __setmtime__ *path* *mtime*](#7)  
[*fsindexHandle* __unset__ *path*](#8)  
[*fsindexHandle* __delete__](#9)  
[*fsindexHandle* __getmetadata__ *name* ?*defaultValue*?](#10)  
[*fsindexHandle* __setmetadata__ *name* *value*](#11)  
[*fsindexHandle* __unsetmetadata__ *name*](#12)  
[*fsindexHandle* __getblockusage__ *block*](#13)  
[*fsindexHandle* __changecount__](#14)  
[*fsindexHandle* __fileset__ *fileset*](#15)  

# <a name='description'></a>DESCRIPTION

Cookfs fsindex provide a low level mechanism for storing mapping between files
in cookfs archive and pages they correspond to\.

This mechanism should only be used by advanced users and is only documented for
reference\.

# <a name='section2'></a>COMMANDS

  - <a name='1'></a>__::cookfs::fsindex__ ?*exportedData*?

    Creates a new fsindex object\. If *exportedData* is specified, index is
    imported from this data and error is thrown if data is not correct\.

    If *exportedData* is not provided, new fsindex is created\.

    The command returns a fsindex command that can be used to perform actions on
    index\.

  - <a name='2'></a>*fsindexHandle* __export__

    Export current index as binary data\. This data can be stored in
    __cookfs\_pages__ object and later on imported using
    __cookfs::fsindex__ command\.

  - <a name='3'></a>*fsindexHandle* __list__ *path*

    List all entries in specified path\. Path should be separated by slashes\.
    Specify empty path to list contents of main directory for archive\.

  - <a name='4'></a>*fsindexHandle* __get__ *path*

    Get information about specified entry\. Path should be separated by slashes\.

    Method __get__ returns file information as a list\. For directories, this
    is a single\-element list containing file modification time\. For files, first
    element is modification time, second element is file size and third element
    is a list of block\-offset\-size triplets\.

    See [BLOCK\-OFFSET\-SIZE INFORMATION](#section3) for more details on
    block\-offset\-size triplets\.

  - <a name='5'></a>*fsindexHandle* __getmtime__ *path*

    Get file modification about specified entry\. Path should be separated by
    slashes\.

  - <a name='6'></a>*fsindexHandle* __set__ *path* *mtime* ?*data*?

    Set information about specified entry\. Path should be separated by slashes\.
    If *data* is omitted, new entry is created as directory\.

    If *data* specified, entry is created as file and data is created as
    block\-offset\-size triplets\.

    See [BLOCK\-OFFSET\-SIZE INFORMATION](#section3) for more details on
    block\-offset\-size triplets\.

  - <a name='7'></a>*fsindexHandle* __setmtime__ *path* *mtime*

    Set file modification about specified entry\. Path should be separated by
    slashes\.

  - <a name='8'></a>*fsindexHandle* __unset__ *path*

    Deletes specified entry\. Path should be separated by slashes\.

  - <a name='9'></a>*fsindexHandle* __delete__

    Delete fsindex object\. Frees up all memory associated with an index\.

  - <a name='10'></a>*fsindexHandle* __getmetadata__ *name* ?*defaultValue*?

    Get metadata with specified *name* from an index, if it exists\. If
    metadata does not exist, *defaultValue* is returned if specified\. If
    metadata does not exist and *defaultValue* is not specified, an error is
    thrown\.

  - <a name='11'></a>*fsindexHandle* __setmetadata__ *name* *value*

    Set metadata with specified *name* to *value*\. Overwrites previous value
    if it existed\.

  - <a name='12'></a>*fsindexHandle* __unsetmetadata__ *name*

    Removes metadata with specified name from index\.

  - <a name='13'></a>*fsindexHandle* __getblockusage__ *block*

    Returns the number of files using the specified block \(index of cookfs
    page\)\. Returns zero if the specified block is not used by any file\.

  - <a name='14'></a>*fsindexHandle* __changecount__

    Returns a counter corresponding to the number of changes to an index since
    the last import/export or create operation\. Initially, this value is
    __0__\.

    This counter is incremented by one after each __set__, __unset__,
    __setmetadata__, __unsetmetadata__ or __setmtime__ operation\.

    This method can be used to check whether changes have been made to an index
    and whether it should be saved before deleting it\.

  - <a name='15'></a>*fsindexHandle* __fileset__ *fileset*

    If argument *fileset* is specified and the fsindex is new, then this
    argument sets the file set mode or the name of the new file set\.

    If argument *fileset* is specified and the fsindex is not new, then this
    argument selects the active file set mode\.

    If argument *fileset* is not specified, then it returns a list of known
    file sets\. The active mode will be in the first item in the list\. If the
    file set mode is not active, then an empty list will be returned\.

    See __cookfs__ for more details on filesets in cookfs\.

# <a name='section3'></a>BLOCK\-OFFSET\-SIZE INFORMATION

Files in cookfs are described storing block\-offset\-size triplets\. These specify
block \(index of cookfs page\), offset in the page and size of the data in a page\.
Each 3 elements of a list describe one part of a file\.

For example __\{1 0 2048\}__ specifies that page with index __1__ should
be used, data starts at offset __0__ and that data is __2048__ bytes\.
Index data of __\{0 0 65536 1 10240 40960\}__ specifies that data is first
stored in page __0__ and uses __65536__ bytes from this page\. Next, data
is stored in page __1__ from __10240__\-th byte and uses up another
__40960__ bytes\. This means that file's total size is 106496 bytes\.

Cookfs index only stores block\-offset\-size triplets and calculates size based on
all sizes of its elements\. Therefore setting cookfs data only requires
specifying list itself, not summed up size of the file\.

# <a name='seealso'></a>SEE ALSO

cookfs\_fsindex

# <a name='keywords'></a>KEYWORDS

cookfs, vfs

# <a name='copyright'></a>COPYRIGHT

Copyright &copy; 2010\-2011 Wojciech Kocjan <wojciech@kocjan\.org>  
Copyright &copy; 2024 Konstantin Kushnir <chpock@gmail\.com>
