#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "botlib.h"

/* For each bot command, private message or group message (but this only works
 * if the bot is set as group admin), this function is called in a new thread,
 * with its private state, sqlite database handle and so forth.
 *
 * For group messages, this function is ONLY called if one of the patterns
 * specified as "triggers" in startBot() matched the message. Otherwise we
 * would spawn threads too often :) */
void handleRequest(int type, int64_t from, int64_t target, sqlite3 *dbhandle, char *request, int argc, sds *argv)
{
    UNUSED(type);

    char buf[256];
    char *where = type == TB_TYPE_PRIVATE ? "privately" : "publicly";
    snprintf(buf, sizeof(buf), "I just %s received: %s", where, request);
    botSendMessage(target,buf,0);

    /* Words received in this request. */
    for (int j = 0; j < argc; j++)
        printf("%d. %s | ", j, argv[j]);
    printf("\n");

    /* Let's use our key-value store API on top of Sqlite. If the
     * user in a Telegram group tells "foo is bar" we will set the
     * foo key to bar. Then if somebody write "foo?" and we have an
     * associated key, we reply with what "foo" is. */
    if (argc >= 3 && !strcasecmp(argv[1],"is")) {
        kvSet(dbhandle,argv[0],request,0);
        /* Note that in this case we don't use 0 as "from" field, so
         * we are sending a reply to the user, not a general message
         * on the channel. */
        botSendMessage(target,"Ok, I'll remember.",from);
    }

    int reqlen = strlen(request);
    if (argc == 1 && reqlen && request[reqlen-1] == '?') {
        char *copy = strdup(request);
        copy[reqlen-1] = 0;
        printf("Looking for key %s\n", copy);
        sds res = kvGet(dbhandle,copy);
        if (res) {
            botSendMessage(target,res,0);
        }
        sdsfree(res);
        free(copy);
    }
}

// This is just called every 1 or 2 seconds. */
void cron(sqlite3 *dbhandle) {
    UNUSED(dbhandle);
    printf("."); fflush(stdout);
}

int main(int argc, char **argv) {
    static char *triggers[] = {
        "Echo *",
        "Hi!",
        "* is *",
        "*\?",
        "!ls",
        NULL,
    };
    startBot(TB_CREATE_KV_STORE, argc, argv, TB_FLAGS_NONE, handleRequest, cron, triggers);
    return 0; /* Never reached. */
}
