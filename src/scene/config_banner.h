#pragma once

#include "scene/surface_shadow.h"

#include <string>
#include <vector>

struct wl_event_source;
struct wlr_scene_tree;

namespace umbriel {
  struct ConfigDiagnostic;
  class Server;

  // Diagnostics panel, drawn with the same chrome as the
  // cheatsheet and the session-quit confirmation: shadow, severity-colored
  // border, rounded background. Errors stay up, warnings auto-hide.
  class ConfigBanner {
  public:
    ConfigBanner(Server& server, wlr_scene_tree* parent);
    ~ConfigBanner();

    ConfigBanner(const ConfigBanner&) = delete;
    ConfigBanner& operator=(const ConfigBanner&) = delete;

    void show(const std::vector<ConfigDiagnostic>& diagnostics, std::string heading = {}, std::string footer = {});
    void hide();
    bool visible() const { return m_tree != nullptr; }
    void relayout();

  private:
    void render(const std::vector<ConfigDiagnostic>& diagnostics);

    Server& m_server;
    wlr_scene_tree* m_parent;
    wlr_scene_tree* m_tree = nullptr;
    SurfaceShadow m_shadow;
    wl_event_source* m_hideTimer = nullptr;
    std::vector<ConfigDiagnostic> m_lastDiagnostics;
    std::string m_heading;
    std::string m_footer;
    bool m_persistent = false; // true when errors present
  };

} // namespace umbriel
