#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "../emec.h"

namespace esphome {
namespace emec {

// ON when the mirrored field is non-zero (alarm flag, output, "minutes > 0").
class EmecBinarySensor : public binary_sensor::BinarySensor, public EmecListener {
 public:
  void on_emec_value(int value) override { this->publish_state(value != 0); }
};

}  // namespace emec
}  // namespace esphome
