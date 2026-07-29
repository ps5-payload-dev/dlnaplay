// SPDX-License-Identifier: GPL-3.0-or-later

#include <dirent.h>

#include <algorithm>
#include <cmath>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "browse/lua_source.h"

// LuaJIT implements the Lua 5.1 API; luaL_len arrived in 5.2.
#if LUA_VERSION_NUM < 502
#define luaL_len lua_objlen
#endif

namespace browse {
  namespace {

    std::string BaseName(const std::string& path) {
      const size_t slash = path.rfind('/');
      return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // Restores the Lua stack top on scope exit so error paths cannot leak
    // values onto the stack.
    class StackGuard {
    public:
      explicit StackGuard(lua_State* L) : L_(L), top_(lua_gettop(L)) {}
      ~StackGuard() { lua_settop(L_, top_); }

    private:
      lua_State* L_;
      int top_;
    };

    // Field readers; all tolerate a missing field ('idx' is the table).

    std::string FieldString(lua_State* L, int idx, const char* key,
			    const std::string& fallback = {}) {
      StackGuard guard(L);
      lua_getfield(L, idx, key);
      // Strings only: lua_tostring on a number would mutate the table value.
      if (lua_type(L, -1) != LUA_TSTRING)
	return fallback;
      size_t len = 0;
      const char* s = lua_tolstring(L, -1, &len);
      return std::string(s, len);
    }

    int64_t FieldInteger(lua_State* L, int idx, const char* key,
			 int64_t fallback) {
      StackGuard guard(L);
      lua_getfield(L, idx, key);
      if (!lua_isnumber(L, -1))
	return fallback;
      return (int64_t)lua_tointeger(L, -1);
    }

    double FieldNumber(lua_State* L, int idx, const char* key, double fallback) {
      StackGuard guard(L);
      lua_getfield(L, idx, key);
      if (!lua_isnumber(L, -1))
	return fallback;
      return lua_tonumber(L, -1);
    }

    Entry::Kind KindFromString(const std::string& s) {
      if (s == "folder") return Entry::Kind::Folder;
      if (s == "audio")  return Entry::Kind::Audio;
      if (s == "video")  return Entry::Kind::Video;
      if (s == "image")  return Entry::Kind::Image;
      return Entry::Kind::Other;
    }

    // Converts the entry table at the top of the stack into 'out'.
    bool ToEntry(lua_State* L, Entry& out, std::string& error) {
      if (!lua_istable(L, -1)) {
	error = "entry is not a table";
	return false;
      }
      const int t = lua_gettop(L);

      out.title = FieldString(L, t, "title");
      if (out.title.empty()) {
	error = "entry has no title";
	return false;
      }
      out.kind = KindFromString(FieldString(L, t, "kind"));
      out.res_url = FieldString(L, t, "url");
      out.id = FieldString(L, t, "id",
			   out.res_url.empty() ? out.title : out.res_url);
      out.child_count = (int)FieldInteger(L, t, "children", -1);

      out.artist = FieldString(L, t, "artist");
      out.album = FieldString(L, t, "album");
      out.genre = FieldString(L, t, "genre");
      out.date = FieldString(L, t, "date");
      out.art_url = FieldString(L, t, "art");
      out.format = FieldString(L, t, "format");
      out.resolution = FieldString(L, t, "resolution");
      out.size_bytes = FieldInteger(L, t, "size", -1);

      const double seconds = FieldNumber(L, t, "duration", -1.0);
      if (seconds >= 0.0)
	out.duration_us = (int64_t)std::llround(seconds * 1e6);
      return true;
    }

    // Converts the array-of-entry-tables at the top of the stack.
    bool ToListing(lua_State* L, Listing& out, std::string& error) {
      if (!lua_istable(L, -1)) {
	error = "browse() did not return a table";
	return false;
      }
      const lua_Integer n = luaL_len(L, -1);
      for (lua_Integer i = 1; i <= n; i++) {
	StackGuard guard(L);
	lua_rawgeti(L, -1, i);
	Entry e;
	if (!ToEntry(L, e, error)) {
	  error = "entry " + std::to_string(i) + ": " + error;
	  return false;
	}
	out.entries.push_back(std::move(e));
      }
      return true;
    }

    std::string PopError(lua_State* L) {
      // Error values are almost always strings; lua_tolstring also converts
      // numbers in place, which is fine since the value is popped right after.
      size_t len = 0;
      const char* s = lua_tolstring(L, -1, &len);
      std::string msg = s ? std::string(s, len) : "(non-string error value)";
      lua_pop(L, 1);
      return msg;
    }
  }

  LuaPlugin::LuaPlugin(const std::string& path)
    : provider_ref(LUA_NOREF), name_(BaseName(path)) {
    L = luaL_newstate();
    if (!L) {
      load_error_ = "cannot create Lua state";
      return;
    }
    luaL_openlibs(L);

    if (luaL_loadfile(L, path.c_str()) != LUA_OK ||
	lua_pcall(L, 0, 1, 0) != LUA_OK) {
      load_error_ = PopError(L);
      return;
    }
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      load_error_ = "script did not return a provider table";
      return;
    }

    const std::string name = FieldString(L, -1, "name");
    if (!name.empty())
      name_ = name;
    provider_ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops the table
  }

  LuaPlugin::~LuaPlugin() {
    if (L)
      lua_close(L);
  }

  LuaSource::LuaSource(LuaPluginPtr plugin, int source_ref, std::string name,
		       std::string detail, std::string icon, std::string root)
    : plugin_(std::move(plugin)), source_ref_(source_ref),
      name_(std::move(name)), detail_(std::move(detail)),
      icon_(std::move(icon)), root_(std::move(root)) {}

  LuaSource::~LuaSource() {
    std::lock_guard<std::mutex> lock(plugin_->mutex);
    luaL_unref(plugin_->L, LUA_REGISTRYINDEX, source_ref_);
  }

  bool LuaSource::Browse(const std::string& id, Listing& out,
			 std::string& error) {
    std::lock_guard<std::mutex> lock(plugin_->mutex);
    lua_State* L = plugin_->L;
    StackGuard guard(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, source_ref_);
    lua_getfield(L, -1, "browse");
    if (!lua_isfunction(L, -1)) {
      error = "source has no browse function";
      return false;
    }
    lua_pushlstring(L, id.data(), id.size());
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
      error = PopError(L);
      return false;
    }
    return ToListing(L, out, error);
  }

  LuaProvider::LuaProvider(const std::string& path)
    : plugin_(std::make_shared<LuaPlugin>(path)) {}

  bool LuaProvider::Discover(std::vector<SourcePtr>& out, std::string& error) {
    if (!plugin_->LoadError().empty()) {
      error = plugin_->LoadError();
      return false;
    }

    std::lock_guard<std::mutex> lock(plugin_->mutex);
    lua_State* L = plugin_->L;
    StackGuard guard(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, plugin_->provider_ref);
    lua_getfield(L, -1, "discover");
    if (!lua_isfunction(L, -1)) {
      error = "plugin has no discover function";
      return false;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
      error = PopError(L);
      return false;
    }
    if (!lua_istable(L, -1)) {
      error = "discover() did not return a table";
      return false;
    }

    std::vector<SourcePtr> found;
    const lua_Integer n = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= n; i++) {
      lua_rawgeti(L, -1, i);
      const int t = lua_gettop(L);
      if (!lua_istable(L, t)) {
	lua_pop(L, 1);
	error = "source " + std::to_string(i) + " is not a table";
	return false;
      }

      const std::string name = FieldString(L, t, "name");
      if (name.empty()) {
	lua_pop(L, 1);
	error = "source " + std::to_string(i) + " has no name";
	return false;
      }
      const std::string detail =
	FieldString(L, t, "detail", plugin_->ProviderName());
      const std::string icon = FieldString(L, t, "icon", "\U0001F9E9");
      const std::string root = FieldString(L, t, "root", "/");

      const int ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops the source table
      found.push_back(std::make_shared<LuaSource>(plugin_, ref, name, detail,
						  icon, root));
    }

    out.insert(out.end(), std::make_move_iterator(found.begin()),
	       std::make_move_iterator(found.end()));
    return true;
  }

  void LoadLuaProviders(const std::string& dir,
			std::vector<std::unique_ptr<Provider>>& out) {
    DIR* d = ::opendir(dir.c_str());
    if (!d)
      return; // no plugin directory, no plugins

    std::vector<std::string> scripts;
    while (struct dirent* de = ::readdir(d)) {
      const std::string name = de->d_name;
      if (name.size() > 4 && name[0] != '.' &&
	  name.compare(name.size() - 4, 4, ".lua") == 0)
	scripts.push_back(dir + "/" + name);
    }
    ::closedir(d);

    std::sort(scripts.begin(), scripts.end());
    for (const std::string& path : scripts)
      out.push_back(std::make_unique<LuaProvider>(path));
  }
}
