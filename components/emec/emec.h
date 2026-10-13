#pragma once
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "emec_protocol.h"
#include "emec_filter.h"

#include <string>
#include <vector>

namespace esphome {
namespace emec {

// Implemented by every entity that mirrors one protocol field. `value` is the
// raw on-wire integer (pH values are x100).
class EmecListener {
 public:
  virtual void on_emec_value(int value) = 0;
};

// ESPHome hub around the pure protocol core (emec_core::EmecProtocol).
// We are the bus master: heartbeat every 500 ms, one full read cycle every
// update_interval, read-modify-write for every setting change. Designed for an
// auto-direction RS-485 transceiver (e.g. MAX13487E): NO DE pin is used.
class EmecComponent : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // --- config setters (from __init__.py) ---
  void set_address(uint8_t a);
  void set_prefix(const std::string &p) { proto_.prefix = p; }
  void set_heartbeat_interval(uint32_t ms) { proto_.hb_interval = ms; }
  void set_startup_delay(uint32_t ms) { proto_.prime_ms = ms; }
  void set_tau(uint8_t t) { proto_.tau = t; }
  // Spike filters on the live readings (raw units: pH x100, mV). 0 = disabled.
  void set_ph_filter(int threshold, uint8_t max_outliers) {
    ph_filter_.threshold = threshold;
    ph_filter_.max_outliers = max_outliers;
  }
  void set_redox_filter(int threshold, uint8_t max_outliers) {
    mv_filter_.threshold = threshold;
    mv_filter_.max_outliers = max_outliers;
  }

  // --- entity <-> hub API ---
  // `always`: notify on every poll (sensors) instead of only on change.
  void register_listener(emec_core::Field field, EmecListener *l, bool always = false);
  void register_connected_listener(EmecListener *l) { connected_listeners_.push_back(l); }
  void register_ph_alarm_listener(EmecListener *l) { ph_alarm_listeners_.push_back(l); }
  void register_redox_alarm_listener(EmecListener *l) { mv_alarm_listeners_.push_back(l); }

  bool is_connected() const { return proto_.online; }
  bool write_field(emec_core::Field field, int value);
  bool reset_alarms();          // also re-arms the pH / Redox spike filters
  bool sync_clock(int hour, int minute, int second, int day, int month, int year);

 protected:
  struct Entry {
    emec_core::Field field;
    EmecListener *listener;
    bool always;
    bool sent;
    int last;
  };

  void publish_group_(emec_core::Group g, bool updated, bool force);
  void run_filters_();
  void publish_filter_alarm_(emec_core::OutlierFilter &f, bool &last, bool &sent,
                             std::vector<EmecListener *> &listeners, const char *what);

  emec_core::EmecProtocol proto_;
  std::vector<Entry> entries_;
  std::vector<EmecListener *> connected_listeners_;
  std::vector<EmecListener *> ph_alarm_listeners_;
  std::vector<EmecListener *> mv_alarm_listeners_;

  emec_core::OutlierFilter ph_filter_;
  emec_core::OutlierFilter mv_filter_;
  bool ph_hold_{false}, mv_hold_{false};          // current reading held back
  bool ph_alarm_last_{false}, mv_alarm_last_{false};
  bool ph_alarm_sent_{false}, mv_alarm_sent_{false};

  bool was_online_{false};
  bool first_cycle_{true};
  uint32_t last_diag_{0};
};

}  // namespace emec
}  // namespace esphome
