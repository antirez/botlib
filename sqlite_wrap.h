#ifndef SQLITE_WRAPPER_H
#define SQLITE_WRAPPER_H

#include <stdint.h>

#define SQL_MAX_SPEC 32     /* Maximum number of ?... specifiers per query. */

/* The sqlCol and sqlRow structures are used in order to return rows. */
typedef struct sqlCol {
    int type;
    int64_t i;          /* Integer or len of string/blob. */
    const char *s;      /* String or blob. */
    double d;           /* Double. */
} sqlCol;

typedef struct sqlRow {
    sqlite3_stmt *stmt; /* Handle for this query. */
    int cols;           /* Number of columns. */
    sqlCol *col;        /* Array of columns. Note that the first time this
                           will be NULL, so we now we don't need to call
                           sqlite3_step() since it was called by the
                           query function. */
} sqlRow;

#endif
