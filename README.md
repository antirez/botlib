# BOTLIB - Telegram C bot framework

Botlib is a C framework to write Telegram bots. It is mainly the sum of two things:

1. An implementation of a subset of the Telegram bot API, wrapped in an event loop that waits for events from the Telegram API and calls our callback in the context of a new thread. The callback that implements the bot has access to various APIs to perform actions in Telegram.
2. A set of higher level wrappers for Sqlite3, JSON, and dynamic strings (SDS library).

## Why this library is written in C?

* First of all, this framework makes writing bots in C a lot more higher level than you could expect. Sqlite3 is exported as a high level API and also exported as a key-value store. Callbacks are called with a structure that already has all the informations about the incoming message, and so forth.
* In the high level languages landscape I had a few bad experiences with libraries changing APIs continuously. A bot is something you write and put online for years: I don't want to babysit code that already works. Bots written with this library will run everywhere as long as you can compile them with `make`. The only dependencies are `libcurl` and `libsqlite3`, which are basically everywhere.
* In the process of writing a few Telegram bots, I found that many requests are quite long living. Think at a bot that transcribes the audio into text, or that uses another external API to fetch information. So multiplexing is not the way to go most of the times: this is why this library uses a thread for each request. And if you use threads like that, you want each thread to be as bare metal as possible, sharing most of the state with the main thread. C is good at that, and the library is implemented so that all the threading issues are transparent for the bot writer.
* Certain bots are quite CPU intensive to run. For instance I wrote a bot that performs analysis on the financial market, and C was a good fit to do Montecaro simulations and things like that.

To give you some feeling about how bots are developed with this framework, see this trivial example, implementing a toy bot:

```c
/* ... standard includes here ... */
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
    /* Only group messages matching this list of glob patterns
     * are passed to the callback. */
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
```

For the full example, showing other API calls, check the `mybot.c` file in this repository.  See further in this README file for the full API specification.

## Installation

Before developing your bot, it's a good idea to be able to compile and run the example as a Telegram bot.

1. Create your bot using the Telegram [@BotFather](https://t.me/botfather).
2. After obtaining your bot API key, store it into a file called `apikey.txt` inside the bot working directory. Alternatively you can use the `--apikey` command line argument to provide your Telegram API key.
3. Optionally edit `mybot.c` to personalized the bot.
3. Build the bot: you need libcurl and libsqlite installed. Just type `make`.
4. Run with `./mybot`. There is also a debug mode if you run it using the `--debug` option (add --debug multiple times for even more verbose messages). For a more moderate output use `--verbose`. Try `mybot --help` for the full list of command line options.
5. Add the bot to your Telegram channel.
6. **IMPORTANT:** The bot *must* be an administrator of the channel in order to read all the messages that are sent in such channel. Private messages will work regardless.

By default the bot will create an SQLite database in the working directory.
If you want to specify another path for your SQLite db, use the `--dbfile`
command line option.

## Telegram APIs

## Sqlite wrapper API

## JSON wrapper API

## SDS strings

... Work in progress ...

For the bot API check `mybot.c` example itself. The basic usage is pretty simple. However for all the other stuff, like the Sqlite3 abstractions, they are taken from [Stonky](https://github.com/antirez/stonky), so the code there (which is very accessible) will provide some help. I hope to document this project better. For now my main goal was to stop duplicating Stonly to create new bots with all the common code inside.
