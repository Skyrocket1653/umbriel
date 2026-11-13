#include "check.h"
#include "config/mouse.h"

#include <utility>

using umbriel::MouseDeviceRule;
using umbriel::MouseRuleSet;

namespace {
  MouseDeviceRule device(std::string name, double raw) {
    MouseDeviceRule rule;
    rule.name = std::move(name);
    rule.settings.rawSensitivityMultiplier = raw;
    return rule;
  }
} // namespace

UMBRIEL_TEST(deviceOverridesInheritUnspecifiedWindowDefaults) {
  MouseRuleSet rules;
  rules.defaults.sensitivityMultiplier = 0.8;
  rules.defaults.rawSensitivityMultiplier = 1.0;
  rules.defaults.wheelMultiplier = 0.5;
  rules.defaults.sensitivityWarning = true;
  rules.devices.push_back(device("Gaming Mouse", 0.01));
  const auto gaming = rules.forDevice("Gaming Mouse");
  CHECK_EQ(gaming.sensitivityMultiplier.value_or(1.0), 0.8);
  CHECK_EQ(gaming.rawSensitivityMultiplier.value_or(1.0), 0.01);
  CHECK_EQ(gaming.wheelMultiplier.value_or(1.0), 0.5);
  CHECK(gaming.sensitivityWarning.value_or(false));
  CHECK_EQ(rules.forDevice("Other Mouse").rawSensitivityMultiplier.value_or(0.0), 1.0);
}

UMBRIEL_TEST(eachDeviceKeepsItsOwnValuesAndMatchingIsExact) {
  MouseRuleSet rules;
  rules.devices.push_back(device("First Mouse", 0.01));
  rules.devices.push_back(device("Second Mouse", 0.05));
  CHECK_EQ(rules.forDevice("First Mouse").rawSensitivityMultiplier.value_or(1.0), 0.01);
  CHECK_EQ(rules.forDevice("Second Mouse").rawSensitivityMultiplier.value_or(1.0), 0.05);
  CHECK(!rules.forDevice("first mouse").rawSensitivityMultiplier.has_value());
  CHECK(!rules.forDevice("First Mouse Extra").rawSensitivityMultiplier.has_value());
  CHECK(!rules.forDevice("").rawSensitivityMultiplier.has_value());
}

UMBRIEL_TEST(laterWindowRulesApplyDefaultsThenMatchingDeviceOverrides) {
  MouseRuleSet base;
  base.defaults.rawSensitivityMultiplier = 1.0;
  base.devices.push_back(device("First Mouse", 0.01));
  MouseRuleSet later;
  later.defaults.rawSensitivityMultiplier = 0.5;
  later.devices.push_back(device("Second Mouse", 0.05));
  MouseRuleSet resolved;
  resolved.overlay(base);
  resolved.overlay(later);
  CHECK_EQ(resolved.forDevice("First Mouse").rawSensitivityMultiplier.value_or(1.0), 0.5);
  CHECK_EQ(resolved.forDevice("Second Mouse").rawSensitivityMultiplier.value_or(1.0), 0.05);
  CHECK_EQ(resolved.forDevice("Other Mouse").rawSensitivityMultiplier.value_or(1.0), 0.5);
}

UMBRIEL_TEST(aRuleForAnotherDeviceDoesNotEraseEarlierOverrides) {
  MouseRuleSet first;
  first.devices.push_back(device("First Mouse", 0.01));
  MouseRuleSet second;
  second.devices.push_back(device("Second Mouse", 0.05));
  MouseRuleSet resolved;
  resolved.overlay(first);
  resolved.overlay(second);
  CHECK_EQ(resolved.forDevice("First Mouse").rawSensitivityMultiplier.value_or(1.0), 0.01);
  CHECK_EQ(resolved.forDevice("Second Mouse").rawSensitivityMultiplier.value_or(1.0), 0.05);
}

UMBRIEL_TEST(laterDeviceRulesMergePerFieldAndRetainZeroAndFalse) {
  MouseRuleSet first;
  auto gaming = device("Gaming Mouse", 0.01);
  gaming.settings.sensitivityMultiplier = 0.8;
  gaming.settings.sensitivityWarning = true;
  first.devices.push_back(gaming);
  MouseRuleSet second;
  MouseDeviceRule override;
  override.name = "Gaming Mouse";
  override.settings.wheelMultiplier = 0.0;
  override.settings.sensitivityWarning = false;
  second.devices.push_back(override);
  MouseRuleSet resolved;
  resolved.overlay(first);
  resolved.overlay(second);
  CHECK_EQ(resolved.devices.size(), size_t{1});
  const auto settings = resolved.forDevice("Gaming Mouse");
  CHECK_EQ(settings.sensitivityMultiplier.value_or(1.0), 0.8);
  CHECK_EQ(settings.rawSensitivityMultiplier.value_or(1.0), 0.01);
  CHECK_EQ(settings.wheelMultiplier.value_or(1.0), 0.0);
  CHECK(!settings.sensitivityWarning.value_or(true));
}

UMBRIEL_TEST(changingDeviceOrItsValuesParticipatesInConfigurationEquality) {
  MouseRuleSet original;
  original.devices.push_back(device("Gaming Mouse", 0.01));
  auto changed = original;
  changed.devices[0].name = "Other Mouse";
  CHECK(changed != original);
  changed = original;
  changed.devices[0].settings.rawSensitivityMultiplier = 0.02;
  CHECK(changed != original);
}

int main() { return RUN_TESTS(); }
