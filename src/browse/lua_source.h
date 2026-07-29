// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>

#include "browse/source.h"

struct lua_State;

namespace browse {

  // One loaded plugin script: owns the lua_State and a registry reference to
  // the provider table the script returned. Shared by the provider and every
  // source it discovers, so an in-flight browse keeps the state alive across
  // a rescan.
  class LuaPlugin {
  public:
    explicit LuaPlugin(const std::string& path);
    ~LuaPlugin();

    LuaPlugin(const LuaPlugin&) = delete;
    LuaPlugin& operator=(const LuaPlugin&) = delete;

    // "" once the script loaded and returned a well-formed provider table.
    const std::string& LoadError() const { return load_error_; }
    const std::string& ProviderName() const { return name_; }

    std::mutex mutex;          // serializes all lua_State access
    lua_State* L = nullptr;    // null if the state could not be created
    int provider_ref;          // registry ref of the provider table

  private:
    std::string name_;         // provider name (falls back to the file name)
    std::string load_error_;
  };

  using LuaPluginPtr = std::shared_ptr<LuaPlugin>;

  class LuaSource : public Source {
  public:
    // Takes ownership of 'source_ref' (registry ref of the source table).
    LuaSource(LuaPluginPtr plugin, int source_ref, std::string name,
	      std::string detail, std::string icon, std::string root);
    ~LuaSource() override;

    const std::string& Name() const override { return name_; }
    const std::string& Detail() const override { return detail_; }
    const char* Icon() const override { return icon_.c_str(); }
    std::string RootId() const override { return root_; }

    bool Browse(const std::string& id, Listing& out,
		std::string& error) override;

  private:
    LuaPluginPtr plugin_;
    int source_ref_;
    std::string name_;
    std::string detail_;
    std::string icon_;
    std::string root_;
  };

  // One plugin script. A script that failed to load still yields a provider
  // so the failure surfaces in the sources-view status line on every scan.
  class LuaProvider : public Provider {
  public:
    explicit LuaProvider(const std::string& path);

    const char* Name() const override { return plugin_->ProviderName().c_str(); }
    bool Discover(std::vector<SourcePtr>& out, std::string& error) override;

  private:
    LuaPluginPtr plugin_;
  };

  // Appends one LuaProvider per *.lua file in 'dir' (sorted by file name).
  // A missing or unreadable directory is not an error; it just adds nothing.
  void LoadLuaProviders(const std::string& dir,
			std::vector<std::unique_ptr<Provider>>& out);

}

