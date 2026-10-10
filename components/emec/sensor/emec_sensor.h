#pragma once
#include "esphome/components/sensor/sensor.h"
#include "../emec.h"

namespace esphome {
namespace emec {

// Read-only numeric value (pH, Redox, service counter...).
class EmecSensor : public sensor::Sensor, public EmecListener {
 public:
  void set_scale(float scale) { scale_ = scale; }
  void on_emec_value(int value) override { this->publish_state(value * scale_); }

 protected:
  float scale_{1.0f};
};

}  // namespace emec
}  // namespace esphome
