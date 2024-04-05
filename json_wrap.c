/* ============================================================================
 * JSON selector implementation: cJSON is a bit too raw...
 * ==========================================================================*/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"

/* You can select things like this:
 *
 * cJSON *json = cJSON_Parse(myjson_string);
 * cJSON *width = cJSON_Select(json,".features.screens[*].width",4);
 * cJSON *height = cJSON_Select(json,".features.screens[4].*","height");
 * cJSON *price = cJSON_Select(json,".features.screens[4].price_*",
 *                  price_type == EUR ? "eur" : "usd");
 *
 * You can use a ":<type>" specifier, usually at the end, in order to
 * check the type of the final JSON object selected. If the type will not
 * match, the function will return NULL. For instance the specifier:
 *
 *  ".foo.bar:s"
 *
 * Will not return NULL only if the root object has a foo field, that is
 * an object with a bat field, that contains a string. This is the full
 * list of selectors:
 *
 *  ".field", select the "field" of the current object.
 *  "[1234]", select the specified index of the current array.
 *  ":<type>", check if the currently selected type is of the specified type,
 *             where the type is a single letter that can be:
 *             "s" for string
 *             "n" for number
 *             "a" for array
 *             "o" for object
 *             "b" for boolean
 *             "!" for null
 *
 * Selectors can be combined, and the special "*" can be used in order to
 * fetch array indexes or field names from the arguments:
 *
 *      cJSON *myobj = cJSON_Select(root,".properties[*].*", index, fieldname);
 */
#define JSEL_INVALID 0
#define JSEL_OBJ 1            /* "." */
#define JSEL_ARRAY 2          /* "[" */
#define JSEL_TYPECHECK 3      /* ":" */
#define JSEL_MAX_TOKEN 256
cJSON *cJSON_Select(cJSON *o, const char *fmt, ...) {
    int next = JSEL_INVALID;        /* Type of the next selector. */
    char token[JSEL_MAX_TOKEN+1];   /* Current token. */
    int tlen;                       /* Current length of the token. */
    va_list ap;

    va_start(ap,fmt);
    const char *p = fmt;
    tlen = 0;
    while(1) {
        /* Our four special chars (plus the end of the string) signal the
         * end of the previous token and the start of the next one. */
        if (tlen && (*p == '\0' || strchr(".[]:",*p))) {
            token[tlen] = '\0';
            if (next == JSEL_INVALID) {
                goto notfound;
            } else if (next == JSEL_ARRAY) {
                if (!cJSON_IsArray(o)) goto notfound;
                int idx = atoi(token); /* cJSON API index is int. */
                if ((o = cJSON_GetArrayItem(o,idx)) == NULL)
                    goto notfound;
            } else if (next == JSEL_OBJ) {
                if (!cJSON_IsObject(o)) goto notfound;
                if ((o = cJSON_GetObjectItemCaseSensitive(o,token)) == NULL)
                    goto notfound;
            } else if (next == JSEL_TYPECHECK) {
                if (token[0] == 's' && !cJSON_IsString(o)) goto notfound;
                if (token[0] == 'n' && !cJSON_IsNumber(o)) goto notfound;
                if (token[0] == 'o' && !cJSON_IsObject(o)) goto notfound;
                if (token[0] == 'a' && !cJSON_IsArray(o)) goto notfound;
                if (token[0] == 'b' && !cJSON_IsBool(o)) goto notfound;
                if (token[0] == '!' && !cJSON_IsNull(o)) goto notfound;
            }
        } else if (next != JSEL_INVALID) {
            /* Otherwise accumulate characters in the current token, note that
             * the above check for JSEL_NEXT_INVALID prevents us from
             * accumulating at the start of the fmt string if no token was
             * yet selected. */
            if (*p != '*') {
                token[tlen] = *p++;
                tlen++;
                if (tlen > JSEL_MAX_TOKEN) goto notfound;
                continue;
            } else {
                /* The "*" character is special: if we are in the context
                 * of an array, we read an integer from the variable argument
                 * list, then concatenate it to the current string.
                 *
                 * If the context is an object, we read a string pointer
                 * from the variable argument string and concatenate the
                 * string to the current token. */
                int len;
                char buf[64];
                char *s;
                if (next == JSEL_ARRAY) {
                    int idx = va_arg(ap,int);
                    len = snprintf(buf,sizeof(buf),"%d",idx);
                    s = buf;
                } else if (next == JSEL_OBJ) {
                    s = va_arg(ap,char*);
                    len = strlen(s);
                } else {
                    goto notfound;
                }
                /* Common path. */
                if (tlen+len > JSEL_MAX_TOKEN) goto notfound;
                memcpy(token+tlen,s,len);
                tlen += len;
                p++;
                continue;
            }
        }
        /* Select the next token type according to its type specifier. */
        if (*p == ']') p++; /* Skip closing "]", it's just useless syntax. */
        if (*p == '\0') break;
        else if (*p == '.') next = JSEL_OBJ;
        else if (*p == '[') next = JSEL_ARRAY;
        else if (*p == ':') next = JSEL_TYPECHECK;
        else goto notfound;
        tlen = 0; /* A new token starts. */
        p++; /* Token starts at next character. */
    }

cleanup:
    va_end(ap);
    return o;

notfound:
    o = NULL;
    goto cleanup;
}
