#include <lua.h>
#include <lauxlib.h>

#define UV_PROXY 1
#define UV_WEAK 2

#define EXTABLES "_extables"

struct extable {
	lua_State *L;
	const void *key;
};

static lua_State *
newdb(lua_State *L, const char * source) {
	lua_State *dL = luaL_newstate();
	// notice: luaL_openlibs if you need corelib
	if (luaL_dofile(dL, source)) {
		// raise error
		lua_pushstring(L, lua_tostring(dL, -1));
		lua_close(dL);
		lua_error(L);
	}
	lua_gc(dL, LUA_GCCOLLECT,0);
	return dL;
}

static void
gettable(lua_State *L, lua_State *dL, int index) {
	// copy table index from dL , and push proxy into L

	// luaL_checktype(dL, -1, LUA_TTABLE);
	const void * key = lua_topointer(dL, index);
	if (key == NULL) {
		luaL_error(L, "Not a table");
	}
	// todo: dL may raise oom error, ignore it to simplify the implementation
	if (lua_rawgetp(dL, LUA_REGISTRYINDEX, key) == LUA_TNIL) {
		lua_pop(dL, 1);
		// put table into registry of dL
		lua_pushvalue(dL, index);
		lua_rawsetp(dL, LUA_REGISTRYINDEX, key);
	} else {
		lua_pop(dL, 1);	// pop table
	}
	if (lua_rawgetp(L, LUA_REGISTRYINDEX, dL) != LUA_TTABLE) {
		lua_pop(L, 1);
		luaL_error(L, "Not an invalid L %p", dL);
	}
	if (lua_rawgetp(L, -1, key) == LUA_TNIL) {
		lua_pop(L, 1);
		struct extable * t = lua_newuserdata(L, sizeof(*t));
		t->L = dL;
		t->key = key;
		lua_pushvalue(L, lua_upvalueindex(UV_PROXY));	// metatable for proxy

		lua_setmetatable(L, -2);
		lua_pushvalue(L, -1);
		lua_rawsetp(L, -3, key);	// cache it
	}
	lua_replace(L, -2);	// remove proxy cache table
}

static int
lopentable(lua_State *L) {
	const char *source = luaL_checkstring(L, 1);

	lua_newtable(L);	// proxy cache table
	lua_pushvalue(L, lua_upvalueindex(UV_WEAK));
	lua_setmetatable(L, -2);

	lua_State *dL = newdb(L, source);

	lua_rawsetp(L, LUA_REGISTRYINDEX, dL);	// notice : may raise oom, and dL is leak

	lua_rawgeti(dL, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
	gettable(L, dL, -1);
	lua_pop(dL, 1);

	// record dL into __extables for closeall
	lua_getfield(L, LUA_REGISTRYINDEX, EXTABLES);
	int n = lua_rawlen(L, -1);
	lua_pushlightuserdata(L, dL);
	lua_rawseti(L, -2, n+1);
	lua_pop(L,1);

	return 1;
}

// push table and key into t
static struct extable *
pretable(lua_State *L) {
	struct extable * t = lua_touserdata(L, 1);
	if (t == NULL) {
		luaL_error(L, "invalid proxy object");
	}
	if (lua_rawgetp(t->L, LUA_REGISTRYINDEX, t->key) != LUA_TTABLE) {
		lua_pop(t->L, 1);
		luaL_error(L, "invalid external table %p of L(%p)", t->key, t->L);
	}
	switch (lua_type(L, 2)) {
	case LUA_TNONE:
	case LUA_TNIL:
		lua_pushnil(t->L);
		break;
	case LUA_TNUMBER:
		if (lua_isinteger(L, 2)) {
			lua_pushinteger(t->L, lua_tointeger(L, 2));
		} else {
			lua_pushnumber(t->L, lua_tonumber(L, 2));
		}
		break;
	case LUA_TSTRING:
		lua_pushstring(t->L, lua_tostring(L, 2));
		break;
	case LUA_TBOOLEAN:
		lua_pushboolean(t->L, lua_toboolean(L, 2));
		break;
	default:
		lua_pop(t->L, 1);
		luaL_error(L, "Unsupport key type %s", lua_typename(L, lua_type(L, 2)));
	}
	return t;
}

static int
copyvalue(lua_State *fromL, lua_State *toL, int index) {
	int t = lua_type(fromL, index);
	switch(t) {
	case LUA_TNIL:
		lua_pushnil(toL);
		break;
	case LUA_TNUMBER:
		if (lua_isinteger(fromL, index)) {
			lua_pushinteger(toL, lua_tointeger(fromL, index));
		} else {
			lua_pushnumber(toL, lua_tonumber(fromL, index));
		}
		break;
	case LUA_TSTRING:
		lua_pushstring(toL, lua_tostring(fromL, index));
		break;
	case LUA_TBOOLEAN:
		lua_pushboolean(toL, lua_toboolean(fromL, index));
		break;
	case LUA_TTABLE:
		gettable(toL, fromL, index);
		break;
	default:
		lua_pushfstring(toL, "Unsupport value type (%s)", lua_typename(fromL, lua_type(fromL, index)));
		return 1;
	}
	return 0;
}

static int
lproxyget(lua_State *L) {
	struct extable *t = pretable(L);
	// notice: use lua_gettable if you need meta index
	lua_rawget(t->L, -2);
	if (copyvalue(t->L, L, -1)) {
		// error
		lua_pop(t->L, 2);
		return lua_error(L);
	}
	lua_pop(t->L, 2);
	return 1;
}

static int
lproxylen(lua_State *L) {
	lua_settop(L, 1);
	struct extable *t = pretable(L);
	size_t n = lua_rawlen(t->L, -2);
	lua_pushinteger(L, n);
	lua_pop(t->L, 2);
	return 1;
}

static int
lproxynext(lua_State *L) {
	struct extable *t = pretable(L);
	if (lua_next(t->L, -2) == 0) {
		lua_pop(t->L, 1);
		return 0;
	}
	if (copyvalue(t->L, L, -2)) {
		// error key
		lua_pop(t->L, 3);
		return lua_error(L);
	}
	if (copyvalue(t->L, L, -1)) {
		// error value
		lua_pop(t->L, 3);
		return lua_error(L);
	}
	return 2;
}

static int
lproxypairs(lua_State *L) {
	lua_pushvalue(L, lua_upvalueindex(1));	// proxynext
	lua_pushvalue(L, 1);	// table
	lua_pushnil(L);
	return 3;
}

// close all extern tables
static int
closeall(lua_State *L) {
	// todo: close all the external L
	lua_getfield(L, LUA_REGISTRYINDEX, EXTABLES);
	int n = lua_rawlen(L, -1);
	int i;
	for (i=1;i<=n;i++) {
		if (lua_rawgeti(L, -1, i) == LUA_TLIGHTUSERDATA) {
			const void *dL = lua_touserdata(L, -1);
			lua_pop(L, 1);
			if (lua_rawgetp(L, LUA_REGISTRYINDEX, dL) == LUA_TTABLE) {
				lua_pop(L, 1);
				lua_pushnil(L);
				lua_rawsetp(L, LUA_REGISTRYINDEX, dL);	// clear cache
				lua_close((lua_State *)dL);
			} else {
				// already close
				lua_pop(L, 1);
			}
		}
	}
	return 0;
}

static int
ltag(lua_State *L) {
	struct extable *t = lua_touserdata(L,1);
	lua_pushfstring(L, "[extable %p:%p]", t->L, t->key);
	return 1;
}

LUAMOD_API int
luaopen_extable(lua_State *L) {
	luaL_checkversion(L);
	lua_newtable(L);

	int modindex = lua_gettop(L);

	// meta table for proxy (upvalue 1:UV_PROXY)
	lua_createtable(L, 0, 1);	// metatable of proxy
	
	lua_pushvalue(L, -1);
	lua_pushcclosure(L, lproxyget, 1);	// binding metatable in upvale 1: UV_PROXY
	lua_setfield(L, -2, "__index");

	lua_pushvalue(L, -1);
	lua_pushcclosure(L, lproxylen, 1);
	lua_setfield(L, -2, "__len");

	lua_pushvalue(L, -1);
	lua_pushcclosure(L, lproxynext, 1);	
	lua_pushcclosure(L, lproxypairs, 1);
	lua_setfield(L, -2, "__pairs");

	lua_pushcfunction(L, ltag);
	lua_setfield(L, -2, "__tostring");


	// weak table (upvalue 2:UV_WEAK)
	lua_createtable(L, 0, 1);
	lua_pushstring(L, "kv");
	lua_setfield(L, -2, "__mode");

	lua_pushcclosure(L, lopentable, 2);

	lua_setfield(L, modindex, "open");


	// keep all extables key into __extables
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, EXTABLES);

	// set collect function when close vm
	lua_createtable(L,0,1);
	lua_pushcfunction(L, closeall);
	lua_setfield(L, -2, "__gc");

	lua_setmetatable(L, -2);

	return 1;
}
