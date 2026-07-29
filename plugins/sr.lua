-- SPDX-License-Identifier: GPL-3.0-or-later

local channels = {
  { name = "P1", url = "https://live1.sr.se/p1-aac-320" },
  { name = "P2", url = "https://live1.sr.se/p2-aac-320" },
  { name = "P3", url = "https://live1.sr.se/p3-aac-320" },
}

return {
  name = "Sveriges Radio",

  discover = function()
    return {
      {
        name = "Sveriges Radio",
        detail = "Live radio from Swedish public service",
        icon = "📻",

        browse = function(id)
          local entries = {}
          for _, channel in ipairs(channels) do
            entries[#entries + 1] = {
              title = channel.name,
              kind = "audio",
              url = channel.url,
              artist = "Sveriges Radio",
              genre = "Radio",
              format = "audio/aac",
            }
          end
          return entries
        end,
      },
    }
  end,
}
