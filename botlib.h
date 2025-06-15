#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#ifndef UNUSED
#define UNUSED(V) ((void) V)
#endif

#include <sqlite3.h>

#include "sds.h"
#include "sqlite_wrap.h"
#include "cJSON.h"

#define TB_FLAGS_NONE 0
#define TB_FLAGS_IGNORE_BAD_ARG (1<<0)

/* This structure is passed to the thread processing a given user request,
 * it's up to the thread to free it once it is done. */
typedef struct BotRequest {
    int type;           /* TB_TYPE_PRIVATE, ... */
    sds request;        /* The request string. */
    int64_t from;       /* ID of user sending the message. */
    sds from_username;  /* Username of the user sending the message. */
    int64_t target;     /* Target channel/user where to reply. */
    int64_t msg_id;     /* Message ID. */
    sds *argv;          /* Request split to single words. */
    int argc;           /* Number of words. */
    int file_type;      /* TB_FILE_TYPE_* */
    sds file_id;        /* File ID if a file is present in the message.
                         * The file format will be given by file_type. */
    int64_t file_size;  /* Size of the file. */
    int bot_mentioned;  /* True if the bot was explicitly mentioned. */
    sds *mentions;      /* List of mentioned usernames. NULL if there
                           are no mentions. */
    int num_mentions;   /* Number of elements in 'mentions' array. */
} BotRequest;

/* Bot callback type. This must be registed when the bot is initialized.
 * Each time the bot receives a command / message matching the list of
 * trigger strings, it starts a thread and calls this callback. */
typedef void (*TBRequestCallback)(sqlite3 *dbhandle, BotRequest *br);
typedef void (*TBCronCallback)(sqlite3 *dbhandle);

/* Type of request used as arugment of the request callback. */
#define TB_TYPE_UNKNOWN 0
#define TB_TYPE_PRIVATE 1
#define TB_TYPE_GROUP 2
#define TB_TYPE_SUPERGROUP 3
#define TB_TYPE_CHANNEL 4

#define TB_FILE_TYPE_NONE 0
#define TB_FILE_TYPE_VOICE_OGG 1
/* ... More ar missing ... */

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
int botSendMessageAndGetInfo(int64_t target, sds text, int64_t reply_to, int64_t *chat_id, int64_t *message_id);
int botSendMessage(int64_t target, sds text, int64_t reply_to);
int botEditMessageText(int64_t chat_id, int message_id, sds text);
int botSendImage(int64_t target, char *filename);
int botGetFile(BotRequest *br, const char *target_filename);
char *botGetUsername(void);
void freeBotRequest(BotRequest *br);

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
