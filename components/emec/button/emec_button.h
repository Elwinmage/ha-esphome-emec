#pragma once
#include "esphome/components/button/button.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/log.h"
#include "../emec.h"

namespace esphome {
namespace emec {

// Clears the latched alarms (resalw).
class EmecResetAlarmsButton : public button::Button {
 public:
  void set_parent(EmecComponent *parent) { parent_ = parent; }

 protected:
  void press_action() override {
    if (parent_ != nullptr) parent_->reset_alarms();
  }

  EmecComponent *parent_{nullptr};
};

// Pushes the ESP wall-clock time to the controller (clockw).
class EmecSyncClockButton : public button::Button {
 public:
  void set_parent(EmecComponent *parent) { parent_ = parent; }
  void set_time(time::RealTimeClock *clock) { clock_ = clock; }

 protected:
  void press_action() override {
    if (parent_ == nullptr || clock_ == nullptr) return;
    auto now = clock_->now();
    if (!now.is_valid()) {
      ESP_LOGW("emec.button", "clock not synchronised yet, nothing sent");
      return;
    }
    parent_->sync_clock(now.hour, now.minute, now.second, now.day_of_month, now.month, now.year);
  }

  EmecComponent *parent_{nullptr};
  time::RealTimeClock *clock_{nullptr};
};

}  // namespace emec
}  // namespace esphome
