#pragma once
#include "esphome/components/switch/switch.h"
#include "../emec.h"

namespace esphome {
namespace emec {

// Writable 0/1 setting (digital input contact type, ON = N.C.). The state is
// not published optimistically: it follows the read-back after the write.
class EmecSwitch : public switch_::Switch, public EmecListener {
 public:
  void set_parent(EmecComponent *parent, emec_core::Field field) {
    parent_ = parent;
    field_ = field;
  }
  void on_emec_value(int value) override { this->publish_state(value != 0); }

 protected:
  void write_state(bool state) override {
    if (parent_ != nullptr) parent_->write_field(field_, state ? 1 : 0);
  }

  EmecComponent *parent_{nullptr};
  emec_core::Field field_{emec_core::F_COUNT};
};

}  // namespace emec
}  // namespace esphome
