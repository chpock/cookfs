/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_H
#define COOKFS_H 1

#include <tcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TODO: provide a better logging mechanism? */
#if 0
#define CookfsLog(a) {a; printf("\n"); fflush(stdout);}
#else
#define CookfsLog(a) {}
#endif

#include "common.h"

#include "pages.h"
#include "pagesCompr.h"
#include "pagesCmd.h"

#include "fsindex.h"
#include "fsindexIO.h"
#include "fsindexCmd.h"

#endif
