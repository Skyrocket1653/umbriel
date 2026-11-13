#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace umbriel {

  struct MouseSettings {
    std::optional<double> sensitivityMultiplier;
    std::optional<double> rawSensitivityMultiplier;
    std::optional<double> wheelMultiplier;
    std::optional<bool> sensitivityWarning;

    void overlay(const MouseSettings& other) {
      if (other.sensitivityMultiplier) {
        sensitivityMultiplier = other.sensitivityMultiplier;
      }
      if (other.rawSensitivityMultiplier) {
        rawSensitivityMultiplier = other.rawSensitivityMultiplier;
      }
      if (other.wheelMultiplier) {
        wheelMultiplier = other.wheelMultiplier;
      }
      if (other.sensitivityWarning) {
        sensitivityWarning = other.sensitivityWarning;
      }
    }

    bool operator==(const MouseSettings&) const = default;
  };

  struct MouseDeviceRule {
    std::string name;
    MouseSettings settings;
    bool operator==(const MouseDeviceRule&) const = default;
  };

  struct MouseRuleSet {
    MouseSettings defaults;
    std::vector<MouseDeviceRule> devices;

    // Apply each matching window rule's defaults, then its device-specific overrides, in window-rule order.
    void overlay(const MouseRuleSet& other) {
      defaults.overlay(other.defaults);
      for (auto& device : devices) {
        device.settings.overlay(other.defaults);
      }
      for (const auto& override : other.devices) {
        const auto found = std::ranges::find(devices, override.name, &MouseDeviceRule::name);
        if (found != devices.end()) {
          found->settings.overlay(override.settings);
        } else {
          devices.push_back(override);
        }
      }
    }

    MouseSettings forDevice(std::string_view name) const {
      MouseSettings result = defaults;
      const auto found = std::ranges::find(devices, name, &MouseDeviceRule::name);
      if (found != devices.end()) {
        result.overlay(found->settings);
      }
      return result;
    }

    bool operator==(const MouseRuleSet&) const = default;
  };

} // namespace umbriel
