#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#ifndef UNUSED
#define UNUSED(V) ((void) V)
#endif

#include <sqlite3.h>

#include "sds.h"
#include "sqlite_wrap.h"
#include "cJSON.h"

/* Bot callback type. This must be registed when the bot is initialized.
 * Each time the bot receives a command / message matching the list of
 * trigger strings, it starts a thread and calls this callback. */
typedef void (*TBRequestCallback)(int type, int64_t from, int64_t target, sqlite3 *dbhandle, char *request, int argc, sds *argv);
typedef void (*TBCronCallback)(sqlite3 *dbhandle);

#define TB_FLAGS_NONE 0
#define TB_FLAGS_IGNORE_BAD_ARG (1<<0)

/* Type of request used as arugment of the request callback. */
#define TB_TYPE_UNKNOWN -1
#define TB_TYPE_PRIVATE 0
#define TB_TYPE_GROUP 1
#define TB_TYPE_SUPERGROUP 2
#define TB_TYPE_CHANNEL 3

/* Concatenate this when starting the bot and passing your create
 * DB query for Sqlite database initialization. */
#define TB_CREATE_KV_STORE \
    "CREATE TABLE IF NOT EXISTS KeyValue(expire INT, " \
                                        "key TEXT, " \
                                        "value BLOB);" \
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_kv_key ON KeyValue(key);" \
    "CREATE INDEX IF NOT EXISTS idx_ex_key ON KeyValue(expire);"

/* Allocation. */
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
void xfree(void *ptr);

/* HTTP */
sds makeHTTPGETCallOpt(const char *url, int *resptr, char **optlist, int optnum);
sds makeHTTPGETCall(const char *url, int *resptr);

/* Telegram bot API. */

int startBot(char *createdb_query, int argc, char **argv, int flags, TBRequestCallback req_callback, TBCronCallback cron_callback, char **triggers);
sds makeGETBotRequest(const char *action, int *resptr, char **optlist, int numopt);
int botSendMessage(int64_t target, sds text, int64_t reply_to);
int botSendImage(int64_t target, char *filename);

/* Database. */
int kvSetLen(sqlite3 *dbhandle, const char *key, const char *value, size_t vlen, int64_t expire);
int kvSet(sqlite3 *dbhandle, const char *key, const char *value, int64_t expire);
sds kvGet(sqlite3 *dbhandle, const char *key);
void kvDel(sqlite3 *dbhandle, const char *key);
void sqlEnd(sqlRow *row);
int sqlNextRow(sqlRow *row);
int sqlInsert(sqlite3 *dbhandle, const char *sql, ...);
int sqlQuery(sqlite3 *dbhandle, const char *sql, ...);
int sqlSelect(sqlite3 *dbhandle, sqlRow *row, const char *sql, ...);
int sqlSelectOneRow(sqlite3 *dbhandle, sqlRow *row, const char *sql, ...);
int64_t sqlSelectInt(sqlite3 *dbhandle, const char *sql, ...);

/* Json */
cJSON *cJSON_Select(cJSON *o, const char *fmt, ...);

#endif
