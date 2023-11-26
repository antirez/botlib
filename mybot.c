#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "botlib.h"

/* For each bot command, private message or group message (but this only works
 * if the bot is set as group admin), this function is called in a new thread,
 * with its private state, sqlite database handle and so forth.
 *
 * For group messages, this function is ONLY called if one of the patterns
 * specified as "triggers" in startBot() matched the message. Otherwise we
 * would spawn threads too often :) */
void handleRequest(sqlite3 *dbhandle, BotRequest *br) {
    char buf[256];
    char *where = br->type == TB_TYPE_PRIVATE ? "privately" : "publicly";
    snprintf(buf, sizeof(buf), "I just %s received: %s", where, br->request);

    int64_t sent_chat_id, sent_message_id;
    botSendMessageAndGetInfo(br->target,buf,0,&sent_chat_id,&sent_message_id);
    printf("Sent message IDs: chat_id:%lld message_id:%lld\n",
        (long long) sent_chat_id, (long long) sent_message_id);

    /* Edit message after 1 second. */
    sleep(1);
    snprintf(buf, sizeof(buf), "I just %s received: %s :D", where, br->request);
    botEditMessageText(sent_chat_id,sent_message_id,buf);

    /* Words received in this request. */
    for (int j = 0; j < br->argc; j++)
        printf("%d. %s | ", j, br->argv[j]);
    printf("is was mentioned? %d | ", br->bot_mentioned);
    if (br->mentions) {
        printf("mentions: ");
        for (int j = 0; j < br->num_mentions; j++)
            printf("%s, ",br->mentions[j]);
    }
    printf("\n");

    /* Show if the message has a voice file inside. */
    if (br->file_type == TB_FILE_TYPE_VOICE_OGG) {
        printf("Voice file ID: %s\n", br->file_id);
        botGetFile(br,"audio.oga");
    }

    /* Let's use our key-value store API on top of Sqlite. If the
     * user in a Telegram group tells "foo is bar" we will set the
     * foo key to bar. Then if somebody write "foo?" and we have an
     * associated key, we reply with what "foo" is. */
    if (br->argc >= 3 && !strcasecmp(br->argv[1],"is")) {
        kvSet(dbhandle,br->argv[0],br->request,0);
        /* Note that in this case we don't use 0 as "from" field, so
         * we are sending a reply to the user, not a general message
         * on the channel. */
        botSendMessage(br->target,"Ok, I'll remember.",br->msg_id);
    }

    int reqlen = strlen(br->request);
    if (br->argc == 1 && reqlen && br->request[reqlen-1] == '?') {
        char *copy = strdup(br->request);
        copy[reqlen-1] = 0;
        printf("Looking for key %s\n", copy);
        sds res = kvGet(dbhandle,copy);
        if (res) {
            botSendMessage(br->target,res,0);
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
