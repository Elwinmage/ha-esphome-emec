#pragma once
#include "esphome/components/select/select.h"
#include "../emec.h"

#include <vector>

namespace esphome {
namespace emec {

// Enum-like writable setting: option N <-> on-wire value wire_[N].
class EmecSelect : public select::Select, public EmecListener {
 public:
  void set_parent(EmecComponent *parent, emec_core::Field field) {
    parent_ = parent;
    field_ = field;
  }
  void set_wire_values(const std::vector<int> &values) { wire_ = values; }

  void on_emec_value(int value) override {
    for (size_t i = 0; i < wire_.size(); i++) {
      if (wire_[i] == value) {
        this->publish_state(i);
        return;
      }
    }
  }

 protected:
  void control(size_t index) override {
    if (parent_ != nullptr && index < wire_.size()) parent_->write_field(field_, wire_[index]);
  }

  EmecComponent *parent_{nullptr};
  emec_core::Field field_{emec_core::F_COUNT};
  std::vector<int> wire_;
};

}  // namespace emec
}  // namespace esphome
