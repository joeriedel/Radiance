// WorldLua.cpp
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH
#include "../App.h"
#include "../Engine.h"
#include "../Game/Game.h"
#include "World.h"
#include "Lua/T_Precache.h"
#include "Lua/T_Spawn.h"
#include "Lua/T_TempSpawn.h"
#include "Lua/D_HTTP.h"
#include "../Persistence.h"
#include "../StringTable.h"
#include "WorldLuaCommon.h"

namespace world {

int WorldLua::lua_System_Platform(lua_State *L) {
	enum {
		PlatPC,
		PlatIPad,
		PlatIPhone,
		PlatXBox360,
		PlatPS3
	};

#if defined(RAD_OPT_WIN) || defined(RAD_OPT_OSX)
	lua_pushinteger(L, PlatPC);
#elif defined(RAD_OPT_IOS)
	lua_pushinteger(L, (App::Get()->deviceFamily == plat::kDeviceFamily_iPhone) ? PlatIPhone : PlatIPad);
#else
	#error RAD_ERROR_UNSUP_PLAT
#endif

	return 1;
}

int WorldLua::lua_System_ScreenSize(lua_State *L) {
	LOAD_SELF

	int vpx, vpy, vpw, vph;
	self->m_world->game->Viewport(vpx, vpy, vpw, vph);

	lua_createtable(L, 0, 2);
	lua_pushinteger(L, vpw);
	lua_setfield(L, -2, "width");
	lua_pushinteger(L, vph);
	lua_setfield(L, -2, "height");

	r::VidMode m(vpw, vph, 32, 60, true);
	if (m.Is3x2()) {
		lua_pushstring(L, "3x2");
	} else if (m.Is4x3()) {
		lua_pushstring(L, "4x3");
	} else if (m.Is16x9()) {
		lua_pushstring(L, "16x9");
	} else {
		lua_pushstring(L, "16x10");
	}
	lua_setfield(L, -2, "aspect");

	return 1;
}

int WorldLua::lua_System_SystemLanguage(lua_State *L) {
	lua_pushinteger(L, (int)App::Get()->langId.get());
	return 1;
}

int WorldLua::lua_System_LaunchURL(lua_State *L) {
	const char *sz = luaL_checkstring(L, 1);
	App::Get()->LaunchURL(sz);
	return 0;
}

int WorldLua::lua_System_Fullscreen(lua_State *L) {
	bool fullscreen = false;
	App *app = App::Get();

	if (app->activeDisplay.get())
		fullscreen = app->activeDisplay->curVidMode->fullscreen;

	lua_pushboolean(L, fullscreen);
	return 1;
}

int WorldLua::lua_System_CreatePrecacheTask(lua_State *L) {
	LOAD_SELF

	Entity *e = EntFramePtr(L, 1, true);

	T_Precache::Ref task = T_Precache::New(
		self->m_world,
		*App::Get()->engine.get(), 
		luaL_checkstring(L, 2), 
		self->m_world->pkgZone,
		lua_toboolean(L, 3) ? true : false,
		(int)lua_tointeger(L, 4)
	);
	
	if (task) {
		if (lua_toboolean(L, 5) == 1) {
			e->QueueScriptTask(boost::static_pointer_cast<Entity::Tickable>(task));
		} else {
			// tick until loaded!
			while (task->Tick(*e, 0.001f, xtime::TimeSlice::Infinite, 0) == TickNext) {}
		}
		task->Push(L);
		return 1;
	}

	return 0;
}

int WorldLua::lua_System_CreateSpawnTask(lua_State *L) {
	LOAD_SELF

	Entity *e = EntFramePtr(L, 1, true);

	Keys keys;
	ParseKeysTable(L, keys, 2, true);
	
	T_Spawn::Ref spawn = T_Spawn::New(self->m_world, keys);
	if (spawn) {
		if (lua_toboolean(L, 3) == 1) {
			e->QueueScriptTask(boost::static_pointer_cast<Entity::Tickable>(spawn));
		} else {
			// tick until loaded!
			while (spawn->Tick(*e, 0.001f, xtime::TimeSlice::Infinite, 0) == TickNext) {}
		}
		spawn->Push(L);
		return 1;
	}

	return 0;
}

int WorldLua::lua_System_CreateTempSpawnTask(lua_State *L) {
	LOAD_SELF

	Entity *e = EntFramePtr(L, 1, true);

	Keys keys;
	ParseKeysTable(L, keys, 2, true);
	
	T_TempSpawn::Ref spawn = T_TempSpawn::New(self->m_world, keys);
	if (spawn) {

		e->QueueScriptTask(boost::static_pointer_cast<Entity::Tickable>(spawn));
		spawn->Push(L);
		return 1;
	}

	return 0;
}

 int WorldLua::lua_System_COut(lua_State *L) {
	 int level = (int)luaL_checkinteger(L, 1);
	 const char *string = luaL_checkstring(L, 2);
	 COut(level) << "(Script):" << string << std::flush;
	 return 0;
 }

 int WorldLua::lua_System_ReadMilliseconds(lua_State *L) {
	 lua_pushnumber(L, xtime::ReadMilliseconds());
	 return 1;
 }

int WorldLua::lua_System_SaveSession(lua_State *L) {
	LOAD_SELF
	
	Persistence::KeyValue::Map *keys = self->m_world->game->session->keys;
	keys->clear();

	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "keys");

	ParseKeysTable(L, *keys, -1, true);
	
	lua_pop(L, 1);
	self->m_world->game->session->Save();
	return 0;
}

int WorldLua::lua_System_LoadSession(lua_State *L) {
	LOAD_SELF
	
	luaL_checktype(L, 1, LUA_TTABLE);
	Persistence::KeyValue::Map *keys = self->m_world->game->session->keys;
	PushKeysTable(L, *keys);
	lua_setfield(L, 1, "keys");
	return 0;
}

int WorldLua::lua_System_SaveGlobals(lua_State *L) {
	Persistence::KeyValue::Map *keys = App::Get()->engine->sys->globals->keys;
	keys->clear();

	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "keys");

	ParseKeysTable(L, *keys, -1, true);
	
	lua_pop(L, 1);
	App::Get()->engine->sys->globals->Save();
	return 0;
}

int WorldLua::lua_System_LoadGlobals(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	Persistence::KeyValue::Map *keys = App::Get()->engine->sys->globals->keys;
	PushKeysTable(L, *keys);
	lua_setfield(L, 1, "keys");
	return 0;
}

int WorldLua::lua_System_CreateSaveGame(lua_State *L) {
	LOAD_SELF

	luaL_checktype(L, 1, LUA_TTABLE);
	self->m_world->game->CreateSaveGame(luaL_checkstring(L, 2));
	PushKeysTable(L, *self->m_world->game->saveGame->keys.get());
	lua_setfield(L, 1, "keys");
	return 0;
}

int WorldLua::lua_System_LoadSavedGame(lua_State *L) {
	LOAD_SELF

	luaL_checktype(L, 1, LUA_TTABLE);
	self->m_world->game->LoadSavedGame(luaL_checkstring(L, 2));
	PushKeysTable(L, *self->m_world->game->saveGame->keys.get());
	lua_setfield(L, 1, "keys");
	return 0;
}

int WorldLua::lua_System_LoadSaveTable(lua_State *L) {
	LOAD_SELF

	PushKeysTable(L, *self->m_world->game->saveGame->keys.get());
	lua_setfield(L, 1, "keys");
	return 0;
}

int WorldLua::lua_System_SaveGame(lua_State *L) {
	LOAD_SELF

	Persistence::KeyValue::Map *keys = self->m_world->game->saveGame->keys;
	keys->clear();

	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "keys");

	ParseKeysTable(L, *keys, -1, true);
	lua_pop(L, 1);

	self->m_world->game->SaveGame();
	return 0;
}

int WorldLua::lua_System_NumSavedGameConflicts(lua_State *L) {
	LOAD_SELF

	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushinteger(L, self->m_world->game->numSavedGameConflicts.get());
	return 1;
}

int WorldLua::lua_System_LoadSavedGameConflict(lua_State *L) {
	LOAD_SELF

	luaL_checktype(L, 1, LUA_TTABLE);
	self->m_world->game->LoadSavedGameConflict((int)luaL_checkinteger(L, 2));
	PushKeysTable(L, *self->m_world->game->saveGame->keys.get());
	lua_setfield(L, 1, "keys");
	return 0;
}

int WorldLua::lua_System_ResolveSavedGameConflict(lua_State *L) {
	LOAD_SELF

	luaL_checktype(L, 1, LUA_TTABLE);
	self->m_world->game->ResolveSavedGameConflict((int)luaL_checkinteger(L, 2));
	return 0;
}

int WorldLua::lua_System_EnableCloudStorage(lua_State *L) {
	LOAD_SELF

	luaL_checktype(L, 1, LUA_TTABLE);
	self->m_world->game->cloudStorage = lua_toboolean(L, 2) ? true : false;
	return 0;
}

int WorldLua::lua_System_CloudFileStatus(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	if (CloudStorage::Enabled()) {
		lua_pushnumber(L, CloudStorage::FileStatus(luaL_checkstring(L, 2)));
	} else {
		lua_pushnumber(L, CloudFile::Ready);
	}
	return 1;
}

int WorldLua::lua_System_StartDownloadingLatestSaveVersion(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	if (CloudStorage::Enabled()) {
		lua_pushboolean(L, CloudStorage::StartDownloadingLatestVersion(luaL_checkstring(L, 2))?1:0);
	} else {
		lua_pushboolean(L, 1);
	}
	return 1;
}

int WorldLua::lua_System_CloudStorageAvailable(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, CloudStorage::Enabled() ? 1 : 0);
	return 1;
}

int WorldLua::lua_System_BuildConfig(lua_State *L) {
#if defined(RAD_OPT_SHIP)
	const char *sz = "ship";
#elif defined(RAD_TARGET_GOLDEN)
	const char *sz = "golden";
#else
	const char *sz = "tools";
#endif
	lua_pushstring(L, sz);
	return 1;
}

int WorldLua::lua_System_UTF8To32(lua_State *L) {
	size_t utf8Len;
	const char *utf8Chars = luaL_checklstring(L, 1, &utf8Len);
	if (!utf8Chars || (utf8Len < 1))
		return 0;

	U32 *buf;
	U32 ubuf[kKilo*4];

	int len = string::utf8to32len(utf8Chars, (int)utf8Len);
	RAD_ASSERT(len > 0);

	if (len > sizeof(ubuf)) {
		buf = (U32*)safe_zone_malloc(ZWorld, sizeof(U32)*len);
	} else {
		buf = ubuf;
	}
	
	string::utf8to32(buf, utf8Chars, (int)utf8Len);
	lua_pushlstring(L, (const char*)buf, len*4);

	if (buf != ubuf)
		zone_free(buf);

	return 1;
}

int WorldLua::lua_System_UTF32To8(lua_State *L) {
	
	size_t utf32Len;
	const U32 *utf32Chars = (const U32*)luaL_checklstring(L, 1, &utf32Len);
	if (!utf32Chars || (utf32Len < 1))
		return 0; // zero length string

	// multiple of 4?
	if (utf32Len&3)
		luaL_error(L, "String does not appear to be UTF32 encoded.");
	
	utf32Len >>= 2;

	char cbuf[kKilo*4*4];
	char *cbufp = cbuf;

	int len = string::utf32to8len(utf32Chars, (int)utf32Len);
	RAD_ASSERT(len > 0);

	if (len > sizeof(cbuf))
		cbufp = (char*)safe_zone_malloc(ZWorld, len);

	string::utf32to8(cbufp, utf32Chars, (int)utf32Len);
	lua_pushlstring(L, cbufp, len);
	
	if (cbufp != cbuf)
		zone_free(cbufp);

	return 1;
}

int WorldLua::lua_System_UTF8Compare(lua_State *L) {
	size_t szaLen;
	const char *sza = luaL_checklstring(L, 1, &szaLen);

	size_t szbLen;
	const char *szb = luaL_checklstring(L, 2, &szbLen);

	const String kStrA(sza, (int)szaLen, string::RefTag);
	const String kStrB(szb, (int)szbLen, string::RefTag);

	lua_pushinteger(L, kStrA.Compare(kStrB));
	return 1;
}

int WorldLua::lua_System_UTF8Comparei(lua_State *L) {
	size_t szaLen;
	const char *sza = luaL_checklstring(L, 1, &szaLen);

	size_t szbLen;
	const char *szb = luaL_checklstring(L, 2, &szbLen);

	const String kStrA(sza, (int)szaLen, string::RefTag);
	const String kStrB(szb, (int)szbLen, string::RefTag);

	lua_pushinteger(L, kStrA.Compare(kStrB));
	return 1;
}

int WorldLua::lua_System_CurrentDateAndTime(lua_State *L) {
	xtime::TimeDate ct = xtime::TimeDate::Now(xtime::TimeDate::local_time_tag);

	lua_createtable(L, 0, 8);
	lua_pushinteger(L, ct.year);
	lua_setfield(L, -2, "year");
	lua_pushinteger(L, ct.millis);
	lua_setfield(L, -2, "millis");
	lua_pushinteger(L, ct.month);
	lua_setfield(L, -2, "month");
	lua_pushinteger(L, ct.dayOfMonth);
	lua_setfield(L, -2, "day");
	lua_pushinteger(L, ct.dayOfWeek);
	lua_setfield(L, -2, "dayOfWeek");
	lua_pushinteger(L, ct.hour);
	lua_setfield(L, -2, "hour");
	lua_pushinteger(L, ct.minute);
	lua_setfield(L, -2, "minute");
	lua_pushinteger(L, ct.second);
	lua_setfield(L, -2, "second");

	return 1;
}

int WorldLua::lua_System_NewHTTPGet(lua_State *L) {
	net::HTTPGet::Ref httpGet(new (ZWorld) net::HTTPGet(ZWorld));
	D_HTTPGet::Ref dhttpGet(D_HTTPGet::New(httpGet));
	dhttpGet->Push(L);
	return 1;
}

int WorldLua::lua_System_UIMode(lua_State *L) {
	LOAD_SELF
	lua_pushinteger(L, (int)self->m_world->game->uiMode.get());
	return 1;
}

void WorldLua::MovieFinished() {
	if (!PushGlobalCall("World.MovieFinished"))
		return;
	Call("World.MovieFinished", 0, 0, 0);
}

void WorldLua::PlainTextDialogResult(bool cancel, const char *text) {
	if (!PushGlobalCall("World.PlainTextDialogResult"))
		return;
	lua_pushboolean(L, cancel ? 1 : 0);
	if (text) {
		lua_pushstring(L, text);
	} else {
		lua_pushnil(L);
	}
	Call("World.PlainTextDialogResult", 2, 0, 0);
}

int WorldLua::lua_System_PlayFullscreenMovie(lua_State *L) {
	LOAD_SELF
	self->m_world->game->PlayFullscreenMovie(luaL_checkstring(L, 1));
	return 0;
}

int WorldLua::lua_System_EnterPlainTextDialog(lua_State *L) {
	LOAD_SELF
	self->m_world->game->EnterPlainTextDialog(
		luaL_checkstring(L, 1),
		luaL_checkstring(L, 2)
	);
	return 0;
}

} // world
