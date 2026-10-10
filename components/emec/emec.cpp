#include "emec.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cstdio>

namespace esphome {
namespace emec {

static const char *const TAG = "emec";

void EmecComponent::set_address(uint8_t a) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u", (unsigned) (a % 100));
  proto_.slave = buf;
}

void EmecComponent::setup() {
  // First read cycle starts as soon as the session is primed.
  proto_.request_poll();
}

void EmecComponent::update() { proto_.request_poll(); }

void EmecComponent::loop() {
  const uint32_t now = millis();

  // 1) drain the UART into the protocol
  uint8_t buf[128];
  int n;
  while ((n = this->available()) > 0) {
    int take = n > (int) sizeof(buf) ? (int) sizeof(buf) : n;
    if (!this->read_array(buf, take)) break;
    proto_.feed(buf, take, now);
  }

  // 2) run the state machine (heartbeat, queries, writes). The auto-direction
  //    transceiver switches to TX on its own; flush() makes sure the frame has
  //    left before we look at the bus again.
  proto_.tick(now, [this](const uint8_t *b, size_t len) {
    this->write_array(b, len);
    this->flush();
  });

  // 3) surface decoded values
  for (uint8_t g = 0; g < emec_core::G_COUNT; g++) {
    if (!proto_.updated[g] && !proto_.force[g]) continue;
    bool updated = proto_.updated[g], force = proto_.force[g];
    proto_.updated[g] = false;
    proto_.force[g] = false;
    this->publish_group_((emec_core::Group) g, updated, force);
  }

  // 4) write results
  for (const auto &e : proto_.events) {
    if (e.ok) {
      ESP_LOGI(TAG, "write %s acknowledged", e.cmd);
    } else {
      ESP_LOGW(TAG, "write %s failed (rejected or no ACK)", e.cmd);
    }
  }
  proto_.events.clear();

  // 5) end of a read cycle -> connection state
  if (proto_.cycle_done) {
    proto_.cycle_done = false;
    if (first_cycle_ || proto_.online != was_online_) {
      if (proto_.online) {
        ESP_LOGI(TAG, "controller online");
      } else {
        ESP_LOGW(TAG, "controller not answering (check wiring, A/B polarity, 38400 8N1)");
      }
      for (auto *l : connected_listeners_) l->on_emec_value(proto_.online ? 1 : 0);
    }
    first_cycle_ = false;
    was_online_ = proto_.online;
  }

  // 6) periodic bus diagnostic
  if (now - last_diag_ > 30000) {
    last_diag_ = now;
    ESP_LOGD(TAG, "bus: rx %u bytes / %u frames, tx %u frames, %u replies, %u timeouts",
             (unsigned) proto_.rx_bytes, (unsigned) proto_.rx_lines, (unsigned) proto_.tx_frames,
             (unsigned) proto_.replies, (unsigned) proto_.timeouts);
  }
}

void EmecComponent::publish_group_(emec_core::Group g, bool updated, bool force) {
  const auto &def = emec_core::GROUPS[g];
  for (auto &e : entries_) {
    if (e.field < def.first || e.field >= def.first + def.count) continue;
    if (!proto_.has(e.field)) continue;
    int v = proto_.value(e.field);
    if (force || !e.sent || v != e.last || (e.always && updated)) {
      e.sent = true;
      e.last = v;
      e.listener->on_emec_value(v);
    }
  }
}

void EmecComponent::register_listener(emec_core::Field field, EmecListener *l, bool always) {
  entries_.push_back({field, l, always, false, 0});
}

bool EmecComponent::write_field(emec_core::Field field, int value) {
  if (!proto_.write_field(field, value)) {
    ESP_LOGW(TAG, "write refused: field %u value %d (out of range or queue full)", (unsigned) field, value);
    // Re-publish the real state so the UI does not keep the rejected value.
    emec_core::Group g = emec_core::group_of(field);
    if (g < emec_core::G_COUNT) proto_.force[g] = true;
    return false;
  }
  return true;
}

bool EmecComponent::reset_alarms() { return proto_.reset_alarms(); }

bool EmecComponent::sync_clock(int hour, int minute, int second, int day, int month, int year) {
  return proto_.sync_clock(hour, minute, second, day, month, year);
}

void EmecComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "EMEC WDPHRH controller:");
  ESP_LOGCONFIG(TAG, "  Prefix / slave: %s / %s", proto_.prefix.c_str(), proto_.slave.c_str());
  ESP_LOGCONFIG(TAG, "  Heartbeat: %u ms", (unsigned) proto_.hb_interval);
  ESP_LOGCONFIG(TAG, "  Startup delay: %u ms", (unsigned) proto_.prime_ms);
  ESP_LOGCONFIG(TAG, "  paramw tau: %u", (unsigned) proto_.tau);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace emec
}  // namespace esphome
