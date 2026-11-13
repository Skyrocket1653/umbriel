# Behavior rules

Behavior keys change how matching windows interact with input, displays, and capture. They update while the window is open and on config reload.

```toml
# Variable refresh rate for game content
[[window_rule]]
match.content_type = "game"
vrr = "always"
```

## Behavior keys

| Key                                | Default | Description                                                                                                                                                                                                                                |
| ---------------------------------- | ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `mouse.sensitivity_multiplier`     | 1.0     | Pointer and accelerated relative-motion multiplier accepting any finite number, default 1.0, on top of device acceleration and sensitivity. Uses the window receiving pointer input.                                                       |
| `mouse.raw_sensitivity_multiplier` | 1.0     | Unaccelerated raw-input multiplier accepting any finite number, default 1.0. Accelerated relative motion uses `mouse.sensitivity_multiplier`, independently of this value.                                                                 |
| `mouse.wheel_multiplier`           | 1.0     | Application mouse-wheel scrolling multiplier accepting any finite number, default 1.0. Scales vertical and horizontal wheel motion, including high-resolution value120 ticks. Touchpad scrolling and compositor navigation are unaffected. |
| `mouse.device`                     | —       | An array of named device overrides for this window. Each entry has an exact, case-sensitive `name` and any of the four mouse settings above or below. Unspecified settings inherit the window defaults.                                    |
| `mouse.sensitivity_warning`        | true    | Show the compositor banner for invalid or oversized scaled mouse or wheel input; default `true`. Set `false` to hide it for this window. Logging and numeric checks remain enabled for mouse and wheel multipliers.                        |
| `confine_pointer`                  | `false` | Keep the pointer inside the window while it has keyboard focus. See [Games](#games).                                                                                                                                                       |
| `allow_shortcuts_inhibit`          | `true`  | Let the app take over compositor shortcuts. See [Keep compositor shortcuts](#keep-compositor-shortcuts).                                                                                                                                   |
| `background_render`                | `false` | Keep drawing while hidden. See [Keep drawing while hidden](#keep-drawing-while-hidden).                                                                                                                                                    |
| `block_capture`                    | `false` | Leave the window out of screenshots and recordings. See [Hide a window from screenshots](#hide-a-window-from-screenshots).                                                                                                                 |
| `focus_on_activate`                | global  | Override `general.focus_on_activate`. See [Focus on activation](#focus-on-activation).                                                                                                                                                     |
| `touchpad_scroll_factor`           | unset   | Touchpad scroll speed multiplier, 0.1 to 10.0. See [Touchpad scroll speed](#touchpad-scroll-speed).                                                                                                                                        |
| `vrr`                              | output  | Override the output's [VRR](display-features.md#variable-refresh-rate) policy: `"disabled"`, `"always"`, or `"fullscreen"`.                                                                                                                |
| `tearing`                          | client  | Request (`true`) or veto (`false`) [tearing](display-features.md#tearing).                                                                                                                                                                 |
| `hdr`                              | output  | Override the output's [HDR](display-features.md#hdr) policy.                                                                                                                                                                               |

`vrr`, `tearing`, and `hdr` apply while the window is focused. A window rule's `vrr = "fullscreen"` follows the window's own fullscreen state.

## Games

### Xwayland compatibility

Use the `xwayland` subtable in a window rule for game compatibility:

```toml
[[window_rule]]
match.is_xwayland = true
match.app_id = "^YOUR_GAME_APP_ID$"
xwayland.preserve_focus = true
xwayland.restore_pointer = true
```

`xwayland.preserve_focus` retains the game's Xwayland activation across compositor focus handoffs. `xwayland.restore_pointer` saves and restores its pointer state when returning to the game. Both default to `false`, apply only to Xwayland windows, and can be enabled or disabled independently. Later matching rules override only the fields they set.

The same settings can be written as `xwayland = { preserve_focus = true, restore_pointer = true }` within the rule. Compositor focus navigation, overview, and exclusive shell keyboard input remain available.

```toml
# Keep the pointer in games that release it in menus
[[window_rule]]
match.app_id = "^steam_app_.*$"
confine_pointer = true
```

- Confinement applies while the window has keyboard focus and the pointer is inside it, tiled, floating, or fullscreen.
- Pointer locks requested by the game still work; confinement resumes when the game releases its lock.
- Escape with a focus or workspace keybind.
- It is suspended during session lock, the overview, and compositor move or resize.

```toml
# Variable refresh and tearing for Steam games
[[window_rule]]
match.app_id = "^steam_app_.*$"
vrr = "always"
tearing = true
```

Tearing also needs `tearing = true` on the output; see [Tearing](display-features.md#tearing).

## Keep compositor shortcuts

```toml
# Compositor shortcuts keep working in virtual machines
[[window_rule]]
match.app_id = "^virt-manager$"
allow_shortcuts_inhibit = false
```

Virtual machines and remote desktops normally capture every key. This denies their request; the [manual passthrough toggle](keybinds.md#keyboard-shortcuts-inhibition) still works.

## Keep drawing while hidden

```toml
# OBS keeps running on another workspace
[[window_rule]]
match.app_id = "^obs$"
background_render = true
```

A window that no output shows, on another workspace, in a hidden tab, or in a hidden scratchpad, is told it is suspended and can stop drawing. Applications that tie other work to drawing can stall. This keeps them running as if shown, at the cost of the CPU and GPU time they spend drawing.

## Hide a window from screenshots

```toml
# Keep a private app out of screenshots and recordings
[[window_rule]]
match.app_id = "^private-app$"
block_capture = true
```

- Output and region captures show what is underneath; no black box replaces the window.
- Direct capture of the window is refused, and enabling the rule stops existing sessions.
- Frames already recorded cannot be recalled.
- The window's popups and transients are protected too.
- Local display, input, focus, and layout are unchanged. Protected captures redraw the whole output.

> [!WARNING]
> While any X11 window is protected, X11 popups without a known parent are also left out of captures, which can hide unrelated X11 menus and notifications.

## Mouse multipliers

Mouse sensitivity rules applyto relative pointer devices, including touchpads, for native Wayland and Xwayland windows. Absolute devices such as tablets keep their absolute positioning. The rule for the window receiving pointer input applies to each motion event; desktop and layer surfaces use 1.0. A locked pointer still receives scaled relative input while its visible position stays fixed. Accelerated relative motion, including Xwayland pointer-warp emulation, uses the same scaled delta as the compositor pointer. Only unaccelerated relative motion uses `mouse.raw_sensitivity_multiplier`. Games using pointer positions or accelerated relative input for their camera follow `mouse.sensitivity_multiplier`. An application drawing its own cursor from raw input follows `mouse.raw_sensitivity_multiplier`; the compositor cannot identify whether that raw input is being used for a cursor or a camera.

```toml
[[window_rule]]
match.app_id = "^steam_app_.*$"
mouse.sensitivity_multiplier = 0.8
mouse.raw_sensitivity_multiplier = 0.5
mouse.wheel_multiplier = 0.5
```

The `mouse.*` keys belong to a nested `mouse` table. They can also be written together as `mouse = { sensitivity_multiplier = 0.8, raw_sensitivity_multiplier = 0.5, wheel_multiplier = 0.5, sensitivity_warning = false }`.

To give different mice different settings within the same window rule, use an array of device overrides:

```toml
[[window_rule]]
match.app_id = "^War Thunder"
mouse.sensitivity_multiplier = 1.0
mouse.raw_sensitivity_multiplier = 1.0
mouse.wheel_multiplier = 1.0
mouse.device = [
    { name = "FIRST MOUSE NAME", raw_sensitivity_multiplier = 0.01, wheel_multiplier = 0.5 },
    { name = "SECOND MOUSE NAME", sensitivity_multiplier = 0.8, raw_sensitivity_multiplier = 0.05 },
]
```

Use the exact, case-sensitive `Device` name reported by `libinput list-devices`, as with [`input.device`](input.md#per-device-overrides). Unlisted devices use the window's defaults. Missing fields inherit those defaults, including `sensitivity_warning`. Devices reporting the same name receive the same settings.

Each device entry can also be written as a `[[window_rule.mouse.device]]` table belonging to the current window rule. Entries with missing, empty, or duplicate names within a window rule are ignored with a warning. For each matching window rule, its defaults apply first and its matching device entry applies second. Window rules are processed in order, so a later rule overrides only the fields it sets for that device.

Each motion component is multiplied directly by its configured factor. The compositor passes scaled relative motion to wlroots for Wayland encoding at 1/256-unit resolution. Very small deltas can round to zero during protocol encoding; fractions are not carried between events.

`mouse.wheel_multiplier` scales wheel and wheel-tilt events delivered to applications. Axis distances are multiplied directly; value120 counts are multiplied and rounded to the nearest integer because the protocol requires integer counts. Small multipliers can round a value120 count to zero. Zero disables application wheel scrolling and negative values reverse it. Compositor wheel keybinds, overview navigation, and tab-bar navigation keep their normal behavior. For touchpad scrolling, use `touchpad_scroll_factor` instead.

Sensitivity multipliers are not clamped: zero disables movement and negative values reverse it. Infinity and NaN are rejected when loading the configuration. Every scaled motion component is checked before use: non-finite results (including multiplication overflow) are discarded, and finite results outside Wayland's signed 24.8 motion range are limited to that encoding range without changing the configured multiplier.

Invalid or oversized movement logs a warning naming the application's app ID and the input device and shows a compositor banner, at most once every ten seconds across all windows. The banner auto-hides after ten seconds and does not replace an already visible configuration diagnostic. Set `mouse.sensitivity_warning = false` in a matching rule to suppress the runtime banner; warnings are still logged. This option does not suppress configuration parsing diagnostics.

This example scales the compositor pointer and accelerated relative input to 80%, unaccelerated raw input to 50%, and application wheel scrolling to 50%. `[input.mouse] sensitivity` still controls device acceleration globally and does not change unaccelerated raw deltas. A later matching rule can reset either multiplier to 1.0 independently.

## Touchpad scroll speed

```toml
# Faster touchpad scrolling in a document viewer
[[window_rule]]
match.app_id = "^org[.]pwmt[.]zathura$"
touchpad_scroll_factor = 2.0
```

The factor multiplies [`input.touchpad.scroll_factor`](input.md#touchpad). Mouse wheels are unaffected.

## Focus on activation

`focus_on_activate` overrides [`general.focus_on_activate`](configuration.md#general) for matching windows: whether an application's activation request focuses and reveals the window.
