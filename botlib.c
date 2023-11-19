/* Copyright (c) 2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Adding these for portablity */
#define _BSD_SOURCE
#if defined(__linux__)
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include <curl/curl.h>
#include <sqlite3.h>

#include "sds.h"
#include "cJSON.h"
#include "botlib.h"

/* Thread local and atomic state. */
_Thread_local sqlite3 *DbHandle = NULL; /* Per-thread sqlite handle. */

/* The bot global state. */
struct {
    int debug;         /* If true enables debugging info (--debug option).
                          This gets incremented by one at every successive
                          --debug, so that very verbose stuff are only
                          enabled with a few --debug calls. */
    int verbose;                        // If true enables verbose info.
    char *dbfile;                       // Change with --dbfile.
    char **triggers;                    // Strings triggering processing.
    sds apikey;                         // Telegram API key for the bot.
    TBRequestCallback callback;         // Callback handling requests.
} Bot;

/* Global stats. Sometimes we access such stats from threads without caring
 * about race conditions, since they in practice are very unlikely to happen
 * in most archs with this data types, and even so we don't care.
 * This stuff is reported by the bot when the $$ info command is used. */
struct {
    time_t start_time;      /* Unix time the bot was started. */
    uint64_t queries;       /* Number of queries received. */
} botStats;

int kvSetLen(const char *key, const char *value, size_t vlen, int64_t expire);
int kvSet(const char *key, const char *value, int64_t expire);
sds kvGet(const char *key);

/* ============================================================================
 * Utils
 * ========================================================================= */

/* Glob-style pattern matching. Return 1 on match, 0 otherwise. */
int strmatch(const char *pattern, int patternLen,
             const char *string, int stringLen, int nocase)
{
    while(patternLen && stringLen) {
        switch(pattern[0]) {
        case '*':
            while (patternLen && pattern[1] == '*') {
                pattern++;
                patternLen--;
            }
            if (patternLen == 1)
                return 1; /* match */
            while(stringLen) {
                if (strmatch(pattern+1, patternLen-1,
                             string, stringLen, nocase))
                    return 1; /* match */
                string++;
                stringLen--;
            }
            return 0; /* no match */
            break;
        case '?':
            string++;
            stringLen--;
            break;
        case '[':
        {
            int not, match;

            pattern++;
            patternLen--;
            not = pattern[0] == '^';
            if (not) {
                pattern++;
                patternLen--;
            }
            match = 0;
            while(1) {
                if (pattern[0] == '\\' && patternLen >= 2) {
                    pattern++;
                    patternLen--;
                    if (pattern[0] == string[0])
                        match = 1;
                } else if (pattern[0] == ']') {
                    break;
                } else if (patternLen == 0) {
                    pattern--;
                    patternLen++;
                    break;
                } else if (patternLen >= 3 && pattern[1] == '-') {
                    int start = pattern[0];
                    int end = pattern[2];
                    int c = string[0];
                    if (start > end) {
                        int t = start;
                        start = end;
                        end = t;
                    }
                    if (nocase) {
                        start = tolower(start);
                        end = tolower(end);
                        c = tolower(c);
                    }
                    pattern += 2;
                    patternLen -= 2;
                    if (c >= start && c <= end)
                        match = 1;
                } else {
                    if (!nocase) {
                        if (pattern[0] == string[0])
                            match = 1;
                    } else {
                        if (tolower((int)pattern[0]) == tolower((int)string[0]))
                            match = 1;
                    }
                }
                pattern++;
                patternLen--;
            }
            if (not)
                match = !match;
            if (!match)
                return 0; /* no match */
            string++;
            stringLen--;
            break;
        }
        case '\\':
            if (patternLen >= 2) {
                pattern++;
                patternLen--;
            }
            /* fall through */
        default:
            if (!nocase) {
                if (pattern[0] != string[0])
                    return 0; /* no match */
            } else {
                if (tolower((int)pattern[0]) != tolower((int)string[0]))
                    return 0; /* no match */
            }
            string++;
            stringLen--;
            break;
        }
        pattern++;
        patternLen--;
        if (stringLen == 0) {
            while(*pattern == '*') {
                pattern++;
                patternLen--;
            }
            break;
        }
    }
    if (patternLen == 0 && stringLen == 0)
        return 1;
    return 0;
}

/* ============================================================================
 * Allocator wrapper: we want to exit on OOM instead of trying to recover.
 * ========================================================================= */

void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (p == NULL) {
        printf("Out of memory: malloc(%zu)", size);
        exit(1);
    }
    return p;
}

void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr,size);
    if (p == NULL) {
        printf("Out of memory: realloc(%zu)", size);
        exit(1);
    }
    return p;
}

void xfree(void *ptr) {
    free(ptr);
}

/* ============================================================================
 * HTTP interface abstraction
 * ==========================================================================*/

/* The callback concatenating data arriving from CURL http requests into
 * a target SDS string. */
size_t makeHTTPGETCallWriter(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    UNUSED(size);
    sds *body = userdata;
    *body = sdscatlen(*body,ptr,nmemb);
    return nmemb;
}

/* Request the specified URL in a blocking way, returns the content (or
 * error string) as an SDS string. If 'resptr' is not NULL, the integer
 * will be set, by referece, to 1 or 0 to indicate error or success.
 * The returned SDS string must be freed by the caller both in case of
 * error and success. */
sds makeHTTPGETCall(const char *url, int *resptr) {
    if (Bot.debug) printf("HTTP GET %s\n", url);
    CURL* curl;
    CURLcode res;
    sds body = sdsempty();

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, makeHTTPGETCallWriter);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15);

        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);
        if (resptr) *resptr = res == CURLE_OK ? 1 : 0;

        /* Check for errors */
        if (res != CURLE_OK) {
            const char *errstr = curl_easy_strerror(res);
            body = sdscat(body,errstr);
        } else {
            /* Return 0 if the request worked but returned a 500 code. */
            long code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            if (code == 500 && resptr) *resptr = 0;
        }

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    return body;
}

/* Like makeHTTPGETCall(), but the list of options will be concatenated to
 * the URL as a query string, and URL encoded as needed.
 * The option list array should contain optnum*2 strings, alternating
 * option names and values. */
sds makeHTTPGETCallOpt(const char *url, int *resptr, char **optlist, int optnum) {
    sds fullurl = sdsnew(url);
    if (optnum) fullurl = sdscatlen(fullurl,"?",1);
    CURL *curl = curl_easy_init();
    for (int j = 0; j < optnum; j++) {
        if (j > 0) fullurl = sdscatlen(fullurl,"&",1);
        fullurl = sdscat(fullurl,optlist[j*2]);
        fullurl = sdscatlen(fullurl,"=",1);
        char *escaped = curl_easy_escape(curl,
            optlist[j*2+1],strlen(optlist[j*2+1]));
        fullurl = sdscat(fullurl,escaped);
        curl_free(escaped);
    }
    curl_easy_cleanup(curl);
    sds body = makeHTTPGETCall(fullurl,resptr);
    sdsfree(fullurl);
    return body;
}

/* Make an HTTP request to the Telegram bot API, where 'req' is the specified
 * action name. This is a low level API that is used by other bot APIs
 * in order to do higher level work. 'resptr' works the same as in
 * makeHTTPGETCall(). */
sds makeGETBotRequest(const char *action, int *resptr, char **optlist, int numopt)
{
    sds url = sdsnew("https://api.telegram.org/bot");
    url = sdscat(url,Bot.apikey);
    url = sdscatlen(url,"/",1);
    url = sdscat(url,action);
    sds body = makeHTTPGETCallOpt(url,resptr,optlist,numopt);
    sdsfree(url);
    return body;
}

/* =============================================================================
 * Higher level Telegram bot API.
 * ===========================================================================*/

/* Send a message to the specified channel, optionally as a reply to a
 * specific message (if reply_to is non zero). */
int botSendMessage(int64_t target, sds text, int64_t reply_to) {
    char *options[10];
    int optlen = 4;
    options[0] = "chat_id";
    options[1] = sdsfromlonglong(target);
    options[2] = "text";
    options[3] = text;
    options[4] = "parse_mode";
    options[5] = "Markdown";
    options[6] = "disable_web_page_preview";
    options[7] = "true";
    if (reply_to) {
        optlen++;
        options[8] = "reply_to_message_id";
        options[9] = sdsfromlonglong(reply_to);
    } else {
        options[9] = NULL; /* So we can sdsfree it later without problems. */
    }

    int res;
    sds body = makeGETBotRequest("sendMessage",&res,options,optlen);
    sdsfree(body);
    sdsfree(options[1]);
    sdsfree(options[9]);
    return res;
}

/* This structure is passed to the thread processing a given user request,
 * it's up to the thread to free it once it is done. */
typedef struct botRequest {
    int type;           /* TB_TYPE_PRIVATE, ... */
    sds request;        /* The request string. */
    int64_t target;     /* Target channel where to reply. */
} botRequest;

/* Free the bot request and associated data. */
void freeBotRequest(botRequest *br) {
    sdsfree(br->request);
    free(br);
}

/* Create a bot request object and return it to the caller. */
botRequest *createBotRequest(void) {
    botRequest *br = malloc(sizeof(*br));
    br->request = NULL;
    br->target = 0;
    br->type = 0;
    return br;
}

/* =============================================================================
 * Database abstraction
 * ===========================================================================*/

/* Create the SQLite tables if needed (if createdb is true), and return
 * the SQLite database handle. Return NULL on error. */
sqlite3 *dbInit(char *createdb_query) {
    sqlite3 *db;
    int rt = sqlite3_open(Bot.dbfile, &db);
    if (rt != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }

    if (createdb_query) {
        char *errmsg;
        int rc = sqlite3_exec(db, createdb_query, 0, 0, &errmsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error [%d]: %s\n", rc, errmsg);
            sqlite3_free(errmsg);
            sqlite3_close(db);
            return NULL;
        }
    }
    return db;
}

/* Should be called every time a thread exits, so that if the thread has
 * an SQLite thread-local handle, it gets closed. */
void dbClose(void) {
    if (DbHandle) sqlite3_close(DbHandle);
    DbHandle = NULL;
}

/* =============================================================================
 * Bot requests handling
 * ========================================================================== */

/* Request handling thread entry point. */
void *botHandleRequest(void *arg) {
    DbHandle = dbInit(NULL);
    botRequest *br = arg;

    /* Parse the request as a command composed of arguments. */
    int argc;
    sds *argv = sdssplitargs(br->request,&argc);

    Bot.callback(br->type, br->target, DbHandle, br->request, argc, argv);

    freeBotRequest(br);
    sdsfreesplitres(argv,argc);
    dbClose();
    return NULL;
}

/* Get the updates from the Telegram API, process them, and return the
 * ID of the highest processed update.
 *
 * The offset is the last ID already processed, the timeout is the number
 * of seconds to wait in long polling in case no request is immediately
 * available. */
int64_t botProcessUpdates(int64_t offset, int timeout) {
    char *options[6];
    int res;

    options[0] = "offset";
    options[1] = sdsfromlonglong(offset+1);
    options[2] = "timeout";
    options[3] = sdsfromlonglong(timeout);
    options[4] = "allowed_updates";
    options[5] = "message";
    sds body = makeGETBotRequest("getUpdates",&res,options,3);
    sdsfree(options[1]);
    sdsfree(options[3]);

    /* Parse the JSON in order to extract the message info. */
    cJSON *json = cJSON_Parse(body);
    cJSON *result = cJSON_Select(json,".result:a");
    if (result == NULL) goto fmterr;
    /* Process the array of updates. */
    cJSON *update;
    cJSON_ArrayForEach(update,result) {
        cJSON *update_id = cJSON_Select(update,".update_id:n");
        if (update_id == NULL) continue;
        int64_t thisoff = (int64_t) update_id->valuedouble;
        if (thisoff > offset) offset = thisoff;

        /* The actual message may be stored in .message or .channel_post
         * depending on the fact this is a private or group message,
         * or, instead, a channel post. */
        cJSON *msg = cJSON_Select(update,".message");
        if (!msg) msg = cJSON_Select(update,".channel_post");
        if (!msg) continue;

        cJSON *chatid = cJSON_Select(msg,".chat.id:n");
        if (chatid == NULL) continue;
        int64_t target = (int64_t) chatid->valuedouble;

        cJSON *chattype = cJSON_Select(msg,".chat.type:s");
        char *ct = chattype->valuestring;
        int type = TB_TYPE_UNKNOWN;
        if (ct != NULL) {
            if (!strcmp(ct,"private")) type = TB_TYPE_PRIVATE;
            else if (!strcmp(ct,"group")) type = TB_TYPE_GROUP;
            else if (!strcmp(ct,"supergroup")) type = TB_TYPE_SUPERGROUP;
            else if (!strcmp(ct,"channel")) type = TB_TYPE_CHANNEL;
        }

        cJSON *date = cJSON_Select(msg,".date:n");
        if (date == NULL) continue;
        time_t timestamp = date->valuedouble;
        cJSON *text = cJSON_Select(msg,".text:s");
        if (text == NULL) continue;
        if (Bot.verbose) printf(".text (target: %lld): %s\n",
            (long long) target,
            text->valuestring);

        /* Sanity check the request before starting the thread:
         * validate that is a request that is really targeting our bot
         * list of "triggers". */
        char *s = text->valuestring;
        if (type != TB_TYPE_PRIVATE && Bot.triggers) {
            int j;
            for (j = 0; Bot.triggers[j]; j++) {
                if (strmatch(Bot.triggers[j], strlen(Bot.triggers[j]),
                    s, strlen(s), 1))
                {
                    break;
                }
            }
            if (Bot.triggers[j] == NULL) continue; // No match.
        }
        if (time(NULL)-timestamp > 60*5) continue; // Ignore stale messages

        /* Spawn a thread that will handle the request. */
        botStats.queries++;
        sds request = sdsnew(text->valuestring);
        botRequest *bt = createBotRequest();
        bt->type = type;
        bt->request = request;
        bt->target = target;
        pthread_t tid;
        if (pthread_create(&tid,NULL,botHandleRequest,bt) != 0) {
            freeBotRequest(bt);
            continue;
        }
        if (Bot.verbose)
            printf("Starting thread to serve: \"%s\"\n",bt->request);
    }

fmterr:
    cJSON_Delete(json);
    sdsfree(body);
    return offset;
}

/* =============================================================================
 * Bot main loop
 * ===========================================================================*/

/* This is the bot main loop: we get messages using getUpdates in blocking
 * mode, but with a timeout. Then we serve requests as needed, and every
 * time we unblock, we check for completed requests (by the thread that
 * handles Yahoo Finance API calls). */
void botMain(void) {
    int64_t nextid = -100; /* Start getting the last 100 messages. */
    int previd;
    while(1) {
        previd = nextid;
        nextid = botProcessUpdates(nextid,1);
        /* We don't want to saturate all the CPU in a busy loop in case
         * the above call fails and returns immediately (for networking
         * errors for instance), so wait a bit at every cycle, but only
         * if we didn't made any progresses with the ID. */
        if (nextid == previd) usleep(100000);
    }
}

/* Check if a file named 'apikey.txt' exists, if so load the Telegram bot
 * API key from there. If the function is able to read the API key from
 * the file, as a side effect the global SDS string Bot.apikey is populated. */
void readApiKeyFromFile(void) {
    FILE *fp = fopen("apikey.txt","r");
    if (fp == NULL) return;
    char buf[1024];
    if (fgets(buf,sizeof(buf),fp) == NULL) {
        fclose(fp);
        return;
    }
    buf[sizeof(buf)-1] = '\0';
    fclose(fp);
    sdsfree(Bot.apikey);
    Bot.apikey = sdsnew(buf);
    Bot.apikey = sdstrim(Bot.apikey," \t\r\n");
}

void resetBotStats(void) {
    botStats.start_time = time(NULL);
    botStats.queries = 0;
}

int startBot(char *createdb_query, int argc, char **argv, int flags, TBRequestCallback callback, char **triggers) {
    srand(time(NULL));

    Bot.debug = 0;
    Bot.verbose = 0;
    Bot.dbfile = "./mybot.sqlite";
    Bot.triggers = triggers;
    Bot.apikey = NULL;
    Bot.callback = callback;

    /* Parse options. */
    for (int j = 1; j < argc; j++) {
        int morearg = argc-j-1;
        if (!strcmp(argv[j],"--debug")) {
            Bot.debug++;
            Bot.verbose = 1;
        } else if (!strcmp(argv[j],"--verbose")) {
            Bot.verbose = 1;
        } else if (!strcmp(argv[j],"--apikey") && morearg) {
            Bot.apikey = sdsnew(argv[++j]);
        } else if (!strcmp(argv[j],"--dbfile") && morearg) {
            Bot.dbfile = argv[++j];
        } else if (!(flags & TB_FLAGS_IGNORE_BAD_ARG)) {
            printf(
            "Usage: %s [--apikey <apikey>] [--debug] [--verbose] "
            "[--dbfile <filename>]"
            "\n",argv[0]);
            exit(1);
        }
    }

    /* Initializations. Note that we don't redefine the SQLite allocator,
     * since SQLite errors are always handled by Stonky anyway. */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (Bot.apikey == NULL) readApiKeyFromFile();
    if (Bot.apikey == NULL) {
        printf("Provide a bot API key via --apikey or storing a file named "
               "apikey.txt in the bot working directory.\n");
        exit(1);
    }
    resetBotStats();
    DbHandle = dbInit(createdb_query);
    if (DbHandle == NULL) exit(1);
    cJSON_Hooks jh = {.malloc_fn = xmalloc, .free_fn = xfree};
    cJSON_InitHooks(&jh);

    /* Enter the infinite loop handling the bot. */
    botMain();
    return 0;
}
