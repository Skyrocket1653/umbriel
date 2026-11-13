#include "config/store.h"
#include "core/log.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "layer/layer_surface.h"
#include "output/output.h"
#include "overview/overview.h"
#include "server/server.h"
#include "view/view.h"
#include "wlr.h"
#include "xwayland/xwayland.h"

#include <algorithm>
#include <cmath>

namespace umbriel {

  namespace {
    constexpr Logger kLog("cursor");
  } // namespace

  void Cursor::handleNewConstraint(wlr_pointer_constraint_v1* constraint) {
    wlr_surface* focused = m_server->seat()->wlr()->pointer_state.focused_surface;
    if (focused != nullptr && focused == constraint->surface) {
      View* view = View::fromSurface(wlr_surface_get_root_surface(focused));
      if (view != nullptr
          && constraint->type != WLR_POINTER_CONSTRAINT_V1_LOCKED
          && view->m_xwaylandPointerResume.has_value()) {
        view->m_xwaylandPointerResume.reset();
        view->syncXwaylandConfigure();
      }
      updateConstraintForSurface(focused);
      if (view != nullptr) {
        finishXwaylandPointerRestore(*view);
      }
    }
  }

  void Cursor::clearConstraint() { setActiveConstraint(nullptr); }

  void Cursor::handleKeyboardFocusChange(wlr_surface* previous, wlr_surface* next) {
    wlr_seat* seat = m_server->seat()->wlr();
    if (seat->drag != nullptr) {
      return;
    }
    wlr_surface* nextRoot = next != nullptr ? wlr_surface_get_root_surface(next) : nullptr;
    if (m_activeConstraint != nullptr && previous != nullptr) {
      wlr_surface* captured = wlr_surface_get_root_surface(m_activeConstraint->surface);
      View* view = View::fromSurface(captured);
      if (view != nullptr && captured == wlr_surface_get_root_surface(previous) && captured != nextRoot) {
        // Native games need the same escape as opted-in X11 games. Save an X baseline before clearing focus, but
        // never require an Xwayland rule to release a native lock or the hidden cursor owned by that old surface.
        rememberLockedXwaylandPointer();
        clearPointerFocus();
        // Reuse keyboard-escape suppression: a new lock request from the old game cannot immediately capture the
        // mouse again while focus is elsewhere. An actual return/re-entry or config reload retains existing policy.
        releaseRuleConfinement(*view);
        wlr_seat_pointer_notify_frame(seat);
        kLog.debug(
            "released client pointer capture on keyboard handoff app_id='{}' xwayland={}",
            view->appId() != nullptr ? view->appId() : "", view->xwayland()
        );
      }
    }
    wlr_surface* pointer = seat->pointer_state.focused_surface;
    if (nextRoot != nullptr && pointer != nullptr && wlr_surface_get_root_surface(pointer) == nextRoot) {
      if (View* view = View::fromSurface(nextRoot); view != nullptr && !view->xwayland()) {
        // A persistent native lock can still exist while focus was elsewhere. Resume it when keyboard focus returns
        // to the same pointer surface; X11 restoration primes and resumes its lock in its own ordered path.
        updateConstraintForSurface(pointer);
      }
    }
  }

  void Cursor::rememberLockedXwaylandPointer() {
    if (m_activeConstraint == nullptr
        || m_activeConstraint->type != WLR_POINTER_CONSTRAINT_V1_LOCKED
        || m_server->sessionLocked()) {
      return;
    }
    wlr_seat* seat = m_server->seat()->wlr();
    wlr_surface* surface = m_activeConstraint->surface;
    View* view = View::fromSurface(wlr_surface_get_root_surface(surface));
    if (view == nullptr
        || surface != view->rootSurface()
        || (!view->preservesXwaylandFocus() && !view->restoresXwaylandPointer())
        || seat->pointer_state.focused_surface != surface) {
      return;
    }
    // Overview saves before it settles layouts; the later input clear must not replace that baseline after geometry
    // has changed. A real restoration clears pending, allowing the next handoff to take a fresh snapshot.
    if (view->m_xwaylandPointerResume.has_value() && view->m_xwaylandPointerResume->pending) {
      return;
    }
    // The lock hint is an unlock destination, not the cursor position the X11 client is currently polling. Xwayland
    // can also have processed a client warp before wlroots receives its corresponding hint. Sample the same X11
    // coordinates as the game; keep the seat's current position only as a fallback if that query fails.
    double x = seat->pointer_state.sx;
    double y = seat->pointer_state.sy;
    bool sampledX11 = false;
    if (view->restoresXwaylandPointer()) {
      if (const Xwayland* xwayland = m_server->xwayland()) {
        if (const auto position = xwayland->pointerPosition(view->xwaylandSurface())) {
          x = (*position)[0];
          y = (*position)[1];
          sampledX11 = true;
        }
      }
    }
    if (std::isfinite(x) && std::isfinite(y)) {
      view->m_xwaylandPointerResume = View::XwaylandPointerResume{.x = x, .y = y};
      const auto& hint = m_activeConstraint->current.cursor_hint;
      kLog.debug(
          "Xwayland pointer saved app_id='{}' x={} y={} source={} seat_x={} seat_y={} hint_enabled={} hint_x={} "
          "hint_y={}",
          view->appId() != nullptr ? view->appId() : "", x, y, sampledX11 ? "x11" : "seat", seat->pointer_state.sx,
          seat->pointer_state.sy, hint.enabled, hint.x, hint.y
      );
    }
  }

  void Cursor::prepareXwaylandPointerRestore(View& view) {
    wlr_seat* seat = m_server->seat()->wlr();
    wlr_surface* surface = view.rootSurface();
    if (!view.restoresXwaylandPointer()
        || !view.m_xwaylandPointerResume.has_value()
        || !view.m_xwaylandPointerResume->pending
        || surface == nullptr
        || m_server->sessionLocked()
        || !isPassthrough()
        || seat->drag != nullptr
        || seat->pointer_state.button_count != 0
        || wlr_seat_pointer_has_grab(seat)
        || wlr_seat_keyboard_has_grab(seat)) {
      return;
    }
    auto* constraint = wlr_pointer_constraints_v1_constraint_for_surface(m_server->pointerConstraints(), surface, seat);
    if (constraint != nullptr && constraint->type != WLR_POINTER_CONSTRAINT_V1_LOCKED) {
      return;
    }
    auto& resume = *view.m_xwaylandPointerResume;
    const double x = std::clamp(resume.x, 0.0, std::max(0.0, static_cast<double>(surface->current.width - 1)));
    const double y = std::clamp(resume.y, 0.0, std::max(0.0, static_cast<double>(surface->current.height - 1)));
    // The old client may not have either compatibility rule. Release its pointer/constraint before priming the game;
    // force a fresh enter even when the old pointer surface happens to be the same, since enter-to-self is a no-op.
    clearPointerFocus();
    clearConstraint();
    view.syncXwaylandConfigure();
    if (Xwayland* xwayland = m_server->xwayland()) {
      xwayland->restorePointerPosition(view.xwaylandSurface(), x, y);
    }
    // Keep Wayland's surface-local baseline aligned too, before the keyboard enter and replacement pointer lock.
    // X11 activation follows on the XWM connection only after restorePointerPosition has completed there.
    wlr_seat_pointer_notify_enter(seat, surface, x, y);
    if (seat->pointer_state.focused_surface == surface) {
      resume.prepared = true;
      kLog.debug(
          "Xwayland pointer primed app_id='{}' x={} y={} lock_present={}", view.appId() != nullptr ? view.appId() : "",
          x, y, constraint != nullptr
      );
      wlr_seat_pointer_notify_frame(seat);
    }
  }

  void Cursor::finishXwaylandPointerRestore(View& view) {
    wlr_seat* seat = m_server->seat()->wlr();
    if (view.restoresXwaylandPointer()
        && view.m_xwaylandPointerResume.has_value()
        && view.m_xwaylandPointerResume->prepared
        && seat->pointer_state.focused_surface == view.rootSurface()
        && View::fromSurface(seat->keyboard_state.focused_surface) == &view) {
      updateConstraintForSurface(view.rootSurface());
      if (m_activeConstraint != nullptr
          && m_activeConstraint->surface == view.rootSurface()
          && m_activeConstraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED
          && view.m_xwaylandPointerResume->pending) {
        view.m_xwaylandPointerResume->pending = false;
        kLog.debug("Xwayland pointer lock restored app_id='{}'", view.appId() != nullptr ? view.appId() : "");
      }
      syncRestoredXwaylandPointer();
    }
  }

  bool Cursor::syncRestoredXwaylandPointer(const Output* output) {
    wlr_seat* seat = m_server->seat()->wlr();
    if (m_server->sessionLocked() || !isPassthrough() || seat->drag != nullptr || wlr_seat_pointer_has_grab(seat)) {
      return false;
    }
    View* view = View::fromSurface(seat->keyboard_state.focused_surface);
    if (view == nullptr
        || !view->restoresXwaylandPointer()
        || !view->mapped()
        || !view->onActiveWorkspace()
        || !view->m_xwaylandPointerResume.has_value()
        || !view->m_xwaylandPointerResume->prepared
        || seat->pointer_state.focused_surface != view->rootSurface()
        || (output != nullptr && view->currentOutput() != output)) {
      return false;
    }
    const bool locked = m_activeConstraint != nullptr
        && m_activeConstraint->surface == view->rootSurface()
        && m_activeConstraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED
        && constraintSurfaceActive();
    if (!locked && !view->m_xwaylandPointerResume->pending) {
      return false;
    }
    int x = 0;
    int y = 0;
    if (wlr_scene_node_coords(&view->m_contentTree->node, &x, &y)) {
      const double scale = surfaceScale(view->rootSurface());
      const double lx = x + seat->pointer_state.sx / scale;
      const double ly = y + seat->pointer_state.sy / scale;
      if ((m_cursor->x != lx || m_cursor->y != ly) && wlr_cursor_warp(m_cursor, nullptr, lx, ly)) {
        forwardEffectPointer();
      }
    }
    // Like an ordinary follows_focus warp, restoring a capture establishes the current hover baseline.
    m_hoverView = view;
    // X11 clients may replace their lock after FocusOut/FocusIn. Keep the primed baseline while that request is in
    // flight and through off-screen animation; do not reinterpret a scene refresh as mouse input. Actual user input
    // cancels an unclaimed restore, so a game that returns to a menu without locking remains usable.
    return true;
  }

  void Cursor::cancelUnclaimedXwaylandPointerRestore() {
    if (m_server->sessionLocked()) {
      return;
    }
    View* view = View::fromSurface(m_server->seat()->wlr()->keyboard_state.focused_surface);
    if (view == nullptr
        || !view->m_xwaylandPointerResume.has_value()
        || !view->m_xwaylandPointerResume->pending
        || !view->m_xwaylandPointerResume->prepared) {
      return;
    }
    if (m_activeConstraint != nullptr
        && m_activeConstraint->surface == view->rootSurface()
        && m_activeConstraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
      finishXwaylandPointerRestore(*view);
      return;
    }
    kLog.debug(
        "Xwayland pointer restore unclaimed; resuming ordinary input app_id='{}'",
        view->appId() != nullptr ? view->appId() : ""
    );
    view->m_xwaylandPointerResume.reset();
    view->syncXwaylandConfigure();
  }

  bool Cursor::pointerInputAllowed(wlr_surface* surface) const {
    if (surface == nullptr) {
      return true;
    }
    View* view = View::fromSurface(wlr_surface_get_root_surface(surface));
    // Only a released game capture is frozen. Ordinary X11 windows still need hover, enter, and click input before
    // they own keyboard focus, including NoInput utilities and drag targets.
    return view == nullptr
        || !view->m_xwaylandPointerResume.has_value()
        || (!view->preservesXwaylandFocus() && !view->restoresXwaylandPointer())
        || View::fromSurface(m_server->seat()->wlr()->keyboard_state.focused_surface) == view;
  }

  void Cursor::onConstraintDestroy(wl_listener* listener, void* /*data*/) {
    Cursor* self;
    self = wl_container_of(listener, self, m_constraintDestroy);
    self->handleConstraintDestroy();
  }

  void Cursor::handleConstraintDestroy() {
    resetMotionRemainders();
    View* view = View::fromSurface(wlr_surface_get_root_surface(m_activeConstraint->surface));
    const bool hadResume = view != nullptr && view->m_xwaylandPointerResume.has_value();
    const bool keepResume = hadResume
        && view->restoresXwaylandPointer()
        && (view->m_xwaylandPointerResume->pending
            || View::fromSurface(m_server->seat()->wlr()->keyboard_state.focused_surface) != view);
    if (keepResume) {
      view->m_xwaylandPointerResume->pending = true;
      kLog.debug(
          "Xwayland lock released during handoff; keeping pointer baseline app_id='{}'",
          view->appId() != nullptr ? view->appId() : ""
      );
    } else {
      if (view != nullptr) {
        view->m_xwaylandPointerResume.reset();
      }
      // An active client releasing its capture (for example, for a menu) owns its final cursor hint.
      warpToConstraintHint(m_activeConstraint);
    }
    wl_list_remove(&m_constraintDestroy.link);
    m_constraintDestroy.link.next = nullptr;
    m_activeConstraint = nullptr;
    if (hadResume && !keepResume) {
      view->syncXwaylandConfigure();
    }
  }

  void Cursor::setActiveConstraint(wlr_pointer_constraint_v1* constraint) {
    if (m_activeConstraint == constraint) {
      return;
    }
    resetMotionRemainders();

    if (m_activeConstraint != nullptr) {
      rememberLockedXwaylandPointer();
      wlr_pointer_constraint_v1* previous = m_activeConstraint;
      m_activeConstraint = nullptr;
      if (m_constraintDestroy.link.next != nullptr) {
        wl_list_remove(&m_constraintDestroy.link);
        m_constraintDestroy.link.next = nullptr;
      }
      warpToConstraintHint(previous);
      wlr_pointer_constraint_v1_send_deactivated(previous);
    }

    m_activeConstraint = constraint;
    if (constraint == nullptr) {
      return;
    }

    m_constraintDestroy.notify = onConstraintDestroy;
    wl_signal_add(&constraint->events.destroy, &m_constraintDestroy);
    wlr_pointer_constraint_v1_send_activated(constraint);
  }

  bool Cursor::constraintSurfaceActive() const {
    if (m_activeConstraint == nullptr || m_activeConstraint->surface == nullptr) {
      return false;
    }
    wlr_surface* root = wlr_surface_get_root_surface(m_activeConstraint->surface);
    if (const LayerSurface* layer = LayerSurface::fromSurface(root)) {
      return layer->mapped();
    }
    View* view = View::fromSurface(root);
    return view != nullptr
        && view->mapped()
        && view->onActiveWorkspace()
        && ((!view->preservesXwaylandFocus() && !view->restoresXwaylandPointer())
            || View::fromSurface(m_server->seat()->wlr()->keyboard_state.focused_surface) == view)
        && (!ruleConstraintApplies(*view) || ruleConstraintEligible(*view));
  }

  void Cursor::updateConstraintForSurface(wlr_surface* surface) {
    if (m_server->sessionLocked() || !isPassthrough()) {
      setActiveConstraint(nullptr);
      return;
    }

    wlr_pointer_constraint_v1* constraint = nullptr;
    if (surface != nullptr) {
      constraint = wlr_pointer_constraints_v1_constraint_for_surface(
          m_server->pointerConstraints(), surface, m_server->seat()->wlr()
      );
      if (View* view = View::fromSurface(wlr_surface_get_root_surface(surface)); view != nullptr
          && ((ruleConstraintApplies(*view) && !ruleConstraintEligible(*view))
              || ((view->preservesXwaylandFocus() || view->restoresXwaylandPointer())
                  && View::fromSurface(m_server->seat()->wlr()->keyboard_state.focused_surface) != view))) {
        constraint = nullptr;
      }
    }
    setActiveConstraint(constraint);
  }

  void Cursor::releaseRuleConfinement(View& view) {
    m_hoverFocusInvalidated = false;
    const char* id = view.extForeignIdentifier();
    m_ruleConfinementView = id != nullptr ? id : "";
    m_ruleConfinementGeneration = configStore().generation();
    if (m_activeConstraint != nullptr
        && View::fromSurface(wlr_surface_get_root_surface(m_activeConstraint->surface)) == &view) {
      clearConstraint();
    }
  }

  bool Cursor::ruleConstraintApplies(View& view) const {
    const char* id = view.extForeignIdentifier();
    return view.confinePointer()
        || (id != nullptr && m_ruleConfinementView == id && m_ruleConfinementGeneration == configStore().generation());
  }

  bool Cursor::ruleConstraintEligible(View& view) const {
    return view.mapped()
        && view.onActiveWorkspace()
        && !m_server->sessionLocked()
        && isPassthrough()
        && !m_server->overview()->active()
        && m_server->seat()->wlr()->drag == nullptr
        && View::fromSurface(m_server->seat()->wlr()->keyboard_state.focused_surface) == &view;
  }

  void Cursor::confineRuleDelta(double* dx, double* dy) {
    View* view = View::fromSurface(m_server->seat()->wlr()->keyboard_state.focused_surface);
    if (view == nullptr || !view->confinePointer() || !ruleConstraintEligible(*view)) {
      return;
    }
    Output* output = view->currentOutput();
    if (output == nullptr) {
      return;
    }
    // Scene coordinates include workspace movement; presented size excludes decorations and shadows.
    // Match the output's scene-root clipping at shared monitor edges.
    wlr_box bounds = view->presentedBox();
    if (!wlr_scene_node_coords(&view->sceneTree()->node, &bounds.x, &bounds.y)) {
      return;
    }
    const wlr_box outputBox = output->layoutBox();
    if (!wlr_box_intersection(&bounds, &bounds, &outputBox)
        || !wlr_box_contains_point(&bounds, m_cursor->x, m_cursor->y)) {
      return;
    }
    // An obscured window must not capture input belonging to a layer surface or another window.
    wlr_surface* surface = nullptr;
    double sx = 0;
    double sy = 0;
    if (m_server->viewAt(m_cursor->x, m_cursor->y, &surface, &sx, &sy) != view) {
      return;
    }
    pixman_region32_t region{};
    pixman_region32_init_rect(&region, bounds.x, bounds.y, bounds.width, bounds.height);
    double x = 0;
    double y = 0;
    if (wlr_region_confine(&region, m_cursor->x, m_cursor->y, m_cursor->x + *dx, m_cursor->y + *dy, &x, &y)) {
      *dx = x - m_cursor->x;
      *dy = y - m_cursor->y;
    }
    pixman_region32_fini(&region);
  }

  void Cursor::warpToConstraintHint(wlr_pointer_constraint_v1* constraint) {
    if (constraint == nullptr || constraint->type != WLR_POINTER_CONSTRAINT_V1_LOCKED) {
      return;
    }
    if (!constraint->current.cursor_hint.enabled) {
      return;
    }

    wlr_seat* seat = m_server->seat()->wlr();
    if (seat->pointer_state.focused_surface != constraint->surface) {
      return;
    }

    // The hint and the seat position are surface-local; the cursor moves in layout units.
    const double scale = surfaceScale(constraint->surface);
    double sx = seat->pointer_state.sx;
    double sy = seat->pointer_state.sy;
    double lx = m_cursor->x + ((constraint->current.cursor_hint.x - sx) / scale);
    double ly = m_cursor->y + ((constraint->current.cursor_hint.y - sy) / scale);
    wlr_cursor_warp(m_cursor, nullptr, lx, ly);
    forwardEffectPointer();
    // Keep wlroots' surface-local pointer state in sync with the layout
    // cursor, avoiding a synthetic jump on the next pointer rebase.
    wlr_seat_pointer_warp(seat, constraint->current.cursor_hint.x, constraint->current.cursor_hint.y);
  }

  bool Cursor::confineDelta(double* dx, double* dy) const {
    if (m_activeConstraint == nullptr) {
      return true;
    }

    wlr_seat* seat = m_server->seat()->wlr();
    if (seat->pointer_state.focused_surface != m_activeConstraint->surface) {
      return true;
    }

    // The region is surface-local; the deltas are in layout units.
    const double scale = surfaceScale(m_activeConstraint->surface);
    double sx = seat->pointer_state.sx;
    double sy = seat->pointer_state.sy;

    pixman_region32_t* region = &m_activeConstraint->region;
    pixman_box32_t* extents = pixman_region32_extents(region);
    pixman_region32_t fullSurface{};
    if (extents->x2 - extents->x1 == 0 || extents->y2 - extents->y1 == 0) {
      // Empty region means the whole surface.
      wlr_surface* surface = m_activeConstraint->surface;
      pixman_region32_init_rect(&fullSurface, 0, 0, surface->current.width, surface->current.height);
      region = &fullSurface;
    }

    double confinedX = 0;
    double confinedY = 0;
    const bool ok = wlr_region_confine(region, sx, sy, sx + (*dx * scale), sy + (*dy * scale), &confinedX, &confinedY);
    if (region == &fullSurface) {
      pixman_region32_fini(&fullSurface);
    }
    if (!ok) {
      return false;
    }

    *dx = (confinedX - sx) / scale;
    *dy = (confinedY - sy) / scale;
    return true;
  }

  double Cursor::surfaceScale(wlr_surface* surface) const {
    const Xwayland* xwayland = m_server->xwayland();
    return xwayland != nullptr ? xwayland->surfaceScale(surface) : 1.0;
  }

} // namespace umbriel
