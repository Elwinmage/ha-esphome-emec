#pragma once
#include "esphome/components/number/number.h"
#include "../emec.h"

#include <cmath>

namespace esphome {
namespace emec {

// Writable numeric setting. The new value is only shown once the pump has
// acknowledged the write and the group has been read back.
class EmecNumber : public number::Number, public EmecListener {
 public:
  void set_parent(EmecComponent *parent, emec_core::Field field, float scale) {
    parent_ = parent;
    field_ = field;
    scale_ = scale;
  }
  void on_emec_value(int value) override { this->publish_state(value * scale_); }

 protected:
  void control(float value) override {
    if (parent_ != nullptr) parent_->write_field(field_, (int) lroundf(value / scale_));
  }

  EmecComponent *parent_{nullptr};
  emec_core::Field field_{emec_core::F_COUNT};
  float scale_{1.0f};
};

}  // namespace emec
}  // namespace esphome
