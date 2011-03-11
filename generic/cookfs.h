/*
 * cookfs.h
 *
 * Main cookfs include file:
 * - includes standard headers
 * - defines (temporary) debug/logging macro definitions
 * - includes additional header files, if enabled in configure
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 */

#ifndef COOKFS_H
#define COOKFS_H 1

#include <tcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef COOKFS_INTERNAL_DEBUG
#define CookfsLog(a) {a; printf("\n"); fflush(stdout);}
#else
#define CookfsLog(a) {}
#endif

#include "common.h"

#ifdef COOKFS_USECPAGES
#include "pages.h"
#include "pagesCmd.h"
#include "pagesCompr.h"
#endif /* COOKFS_USECPAGES */

#ifdef COOKFS_USECFSINDEX
#include "fsindex.h"
#include "fsindexIO.h"
#include "fsindexCmd.h"
#endif /* COOKFS_USECFSINDEX */

#ifdef COOKFS_USECREADERCHAN
#include "readerchannel.h"
#include "readerchannelIO.h"
#endif /* COOKFS_USECREADERCHAN */

#endif /* COOKFS_H */
