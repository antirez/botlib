# BOTLIB - Telegram C bot framework

## Installation

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

## APIs

... Work in progress ...

For the bot API check `mybot.c` example itself. The basic usage is pretty simple. However for all the other stuff, like the Sqlite3 abstractions, they are taken from [Stonky](https://github.com/antirez/stonky), so the code there (which is very accessible) will provide some help. I hope to document this project better. For now my main goal was to stop duplicating Stonly to create new bots with all the common code inside.
