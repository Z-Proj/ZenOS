#include "lprefix.h"
#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "../../userlib.h"

static int os_clock(lua_State *L)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    lua_pushnumber(L, (lua_Number)tv.tv_sec + (lua_Number)tv.tv_usec / 1000000.0);
    return 1;
}

static int os_time(lua_State *L)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    lua_pushinteger(L, (lua_Integer)tv.tv_sec);
    return 1;
}

static int os_difftime(lua_State *L)
{
    lua_Number t2 = luaL_checknumber(L, 1);
    lua_Number t1 = luaL_checknumber(L, 2);
    lua_pushnumber(L, t2 - t1);
    return 1;
}

static int os_date(lua_State *L)
{
    lua_pushstring(L, "date unsupported");
    return 1;
}

static int os_rename(lua_State *L)
{
    const char *from = luaL_checkstring(L, 1);
    const char *to   = luaL_checkstring(L, 2);

    int src = open(from, O_RDONLY);
    if (src < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "cannot open source file");
        return 2;
    }

    int dst = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) {
        close(src);
        if (zen_create(to) != 0) {
            lua_pushnil(L);
            lua_pushstring(L, "cannot create destination file");
            return 2;
        }
        dst = open(to, O_WRONLY, 0644);
        if (dst < 0) {
            close(src);
            lua_pushnil(L);
            lua_pushstring(L, "cannot open destination file");
            return 2;
        }
    }

    char buf[4096];
    int n;
    while ((n = (int)read(src, buf, sizeof(buf))) > 0)
        write(dst, buf, (size_t)n);
    close(src);
    close(dst);
    unlink(from);

    lua_pushboolean(L, 1);
    return 1;
}

static int os_remove(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    if (unlink(path) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int os_getenv(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    char **envp = zen_getenvp();
    if (envp) {
        size_t nlen = strlen(name);
        for (int i = 0; envp[i]; i++) {
            if (strncmp(envp[i], name, nlen) == 0 && envp[i][nlen] == '=') {
                lua_pushstring(L, envp[i] + nlen + 1);
                return 1;
            }
        }
    }
    lua_pushnil(L);
    return 1;
}

static int os_exit(lua_State *L)
{
    int code = lua_isboolean(L, 1)
               ? (lua_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE)
               : (int)luaL_optinteger(L, 1, EXIT_SUCCESS);
    exit(code);
    return 0;
}

static int os_tmpname(lua_State *L)
{
    static int counter = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "/tmp/lua_%04d", counter++);
    lua_pushstring(L, buf);
    return 1;
}

static int os_system(lua_State *L)
{
    (void)L;
    lua_pushinteger(L, -1);
    return 1;
}

static int os_execute(lua_State *L)
{
    (void)L;
    lua_pushnil(L);
    lua_pushstring(L, "no shell on ZenOS");
    lua_pushinteger(L, -1);
    return 3;
}

static int os_setlocale(lua_State *L)
{
    lua_pushstring(L, "C");
    return 1;
}

static const luaL_Reg oslib[] = {
    {"clock",     os_clock},
    {"date",      os_date},
    {"difftime",  os_difftime},
    {"execute",   os_execute},
    {"exit",      os_exit},
    {"getenv",    os_getenv},
    {"remove",    os_remove},
    {"rename",    os_rename},
    {"setlocale", os_setlocale},
    {"system",    os_system},
    {"time",      os_time},
    {"tmpname",   os_tmpname},
    {NULL, NULL}
};

LUAMOD_API int luaopen_os(lua_State *L)
{
    luaL_newlib(L, oslib);
    return 1;
}
