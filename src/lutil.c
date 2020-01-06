#include <errno.h>
#include "lua.h"
#include <string.h>
#include "lauxlib.h"
#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <ctype.h>
#define WIN32_LEAN_AND_MEAN 1
#define NOGDI 1
#include <windows.h>
#endif

char *joinpath(char *pth1, char *pth2)
{
    if (pth1 == NULL && pth2 == NULL)
    {
        return "";
    }
    else if (pth2 == NULL || strlen(pth2) == 0)
    {
        return pth1;
    }
    else if (pth1 == NULL || strlen(pth1) == 0)
    {
        return pth2;
    }
    else
    {
#ifdef _WIN32
        char directory_separator[] = "\\";
#else
        char directory_separator[] = "/";
#endif
        const char *last_char = pth1;
        int append_directory_separator = 0;
        if (pth1[strlen(pth1) - 1] == directory_separator[0])
        {
            char *dst = malloc(strlen(pth1) + strlen(pth2));
            if (!dst)
            {
                return "";
            }
            strcpy(dst, pth1);
            strcat(dst, pth2);
        }
        else
        {
            char *dst = malloc(strlen(pth1) + strlen(pth2) + strlen(directory_separator));
            if (!dst)
            {
                return "";
            }
            strcpy(dst, pth1);
            strcat(dst, directory_separator);
            strcat(dst, pth2);
        }
    }
}

int push_error(lua_State *L, const char *info)
{
    lua_pushnil(L);
    if (info == NULL)
        lua_pushstring(L, strerror(errno));
    else
        lua_pushfstring(L, "%s: %s", info, strerror(errno));
    lua_pushinteger(L, errno);
    return 3;
}

int push_result(lua_State *L, int res, const char *info)
{
    if (res == -1)
    {
        return push_error(L, info);
    }
    else
    {
        lua_pushboolean(L, 1);
        return 1;
    }
}

/*
** Check if the given element on the stack is a file and returns it.
*/
FILE *check_file(lua_State *L, int idx, const char *funcname)
{
    luaL_Stream *fh = (luaL_Stream *)luaL_checkudata(L, idx, "FILE*");
    if (fh->closef == 0 || fh->f == NULL)
    {
        luaL_error(L, "%s: closed file", funcname);
        return NULL;
    }
    else
    {
        return fh->f;
    }
}

/* Push nil, followed by the Windows error message corresponding to
 * the error number, or a string giving the error value in decimal if
 * no error message is found.  If nresults is -2, always push nil and
 * the error message and return 2 even if error is NO_ERROR.  If
 * nresults is -1 and error is NO_ERROR, then push true and return 1.
 * Otherwise, if error is NO_ERROR, return nresults.
 */
#ifdef _WIN32
int windows_pusherror(lua_State *L, DWORD error, int nresults)
{
    if (error != NO_ERROR || nresults == -2)
    {
        char buffer[1024];
        size_t len = sprintf(buffer, "%lu (0x%lX): ", error, error);
        size_t res = FormatMessage(
            FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
            0, error, 0, buffer + len, sizeof buffer - len, 0);
        if (res)
        {
            len += res;
            while (len > 0 && isspace(buffer[len - 1]))
                len--;
        }
        else
        {
            len += sprintf(buffer + len, "<error string not available>");
        }
        lua_pushnil(L);
        lua_pushlstring(L, buffer, len);
        nresults = 2;
    }
    else if (nresults < 0)
    {
        lua_pushboolean(L, 1);
        nresults = 1;
    }
    return nresults;
}
#endif
