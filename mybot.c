#include <stdio.h>

#include "botlib.h"

/* For each bot command, private message or group message (but this only works
 * if the bot is set as group admin), this function is called in a new thread,
 * with its private state, sqlite database handle and so forth.
 *
 * For group messages, this function is ONLY called if one of the patterns
 * specified as "triggers" in startBot() matched the message. Otherwise we
 * would spawn threads too often :) */
void handleRequest(int type, int64_t target, sqlite3 *dbhandle, char *request, int argc, sds *argv)
{
    UNUSED(type);
    UNUSED(dbhandle);
    UNUSED(argc);
    UNUSED(argv);

    char buf[256];
    snprintf(buf, sizeof(buf), "I just received: %s", request);
    botSendMessage(target,buf,0);
}

int main(int argc, char **argv) {
    static char *triggers[] = {
        "Echo *",
        "Hi!",
        NULL,
    };
    startBot(TB_CREATE_KV_STORE, argc, argv, TB_FLAGS_NONE, handleRequest, triggers);
    return 0; /* Never reached. */
}
