#pragma once
// Pure C++ (no ESPHome deps) core of the EMEC WDPHRH "ERMES-family" ASCII
// protocol, so it can be unit-tested off-target (see tests/). The ESPHome
// component owns one of these and only wires UART bytes + entities to it.
//
// Roles on the bus: WE are a master, like the EMEC BT ETH gateway (which may
// stay on the bus: we only transmit when the bus is quiet, and retry).
//
//   heartbeat : "34tb00 #0#\r\n"                 every ~500 ms, keeps the
//                                                pump's RS-485 session alive
//   query     : "3401<cmd>\r\r"                  double CR terminator
//   reply     : "34gpd01&WD#f1#f2#...#<cmd>end\r"
//   write     : "3401<cmd><payload><cmd>end\r\r" payload = the read fields
//                                                concatenated WITHOUT '#',
//                                                zero-padded to fixed widths
//   write ACK : "34gpd01&<cmd>okend\r"
//   rejection : a frame containing "no#" and "end"
//
// Read commands (fields separated by '#'):
//   valuer  : pH(x100), mV, dose, flag
//   setpntr : ph_hi, ph_lo, ph_hi%, ph_lo%, ph_wait, ph_mode,
//             mv_lo, mv_hi, mv_lo%, mv_hi%, mv_wait, mv_mode
//   alldosr : ph_mode, ph_min, mv_mode, mv_min          (dosing alarm)
//   allrmr  : a1..a8                                    (alarm flags)
//   outptr  : out1..out3
//   flowstr : mode, time
//   diginpr : standby, ph_level, mv_level               (0=N.O., 1=N.C.)
//   servicr : offset, hours
//   setstkr : ph_strokes, mv_strokes
//   paramtr : passcode, feed_delay, priority
//   allprbr : ph_mode, ph_min, mv_mode, mv_min          (probe failure)
//
// Every write is a read-modify-write of the whole group: the group is read
// fresh, one field is replaced, the full payload is written back, then the
// group is read again so the entities show what the pump really stored.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace emec_core {

enum Group : uint8_t {
  G_VALUER = 0,
  G_SETPNT,
  G_ALLDOS,
  G_ALLRM,
  G_OUTPT,
  G_FLOWST,
  G_DIGINP,
  G_SERVIC,
  G_SETSTK,
  G_PARAMT,
  G_ALLPRB,
  G_COUNT,
};

// Flat field list. The order inside each group is the on-wire field order.
enum Field : uint8_t {
  // valuer
  F_PH = 0,            // pH x100
  F_MV,                // Redox, mV
  F_DOSE,              // valuer field 3 (unconfirmed)
  F_VALUE_FLAG,        // valuer field 4 (raw)
  // setpntr
  F_PH_HIGH_SP,        // pH x100
  F_PH_LOW_SP,         // pH x100
  F_PH_HIGH_PCT,
  F_PH_LOW_PCT,
  F_PH_WAIT,
  F_PH_MODE,           // 0=PROPORTIONAL, 1=ON/OFF
  F_MV_LOW_SP,
  F_MV_HIGH_SP,
  F_MV_LOW_PCT,
  F_MV_HIGH_PCT,
  F_MV_WAIT,
  F_MV_MODE,           // 0=PROPORTIONAL, 1=ON/OFF
  // alldosr
  F_DOSAL_PH_MODE,     // 0=DOSE, 1=STOP
  F_DOSAL_PH_MIN,      // 0 = disabled
  F_DOSAL_MV_MODE,
  F_DOSAL_MV_MIN,
  // allrmr
  F_ALARM_1,           // pH- reservoir empty
  F_ALARM_2,           // chlorine reservoir empty
  F_ALARM_3,           // no flow
  F_ALARM_4,
  F_ALARM_5,
  F_ALARM_6,
  F_ALARM_7,
  F_ALARM_8,           // standby
  // outptr
  F_OUT1,
  F_OUT2,
  F_OUT3,
  // flowstr
  F_FLOW_MODE,         // 0=Disable, 1=Reverse, 2=Direct
  F_FLOW_TIME,
  // diginpr
  F_DI_STANDBY_NC,
  F_DI_PH_LEVEL_NC,
  F_DI_MV_LEVEL_NC,
  // servicr
  F_SERVICE_OFFSET,
  F_SERVICE_HOURS,
  // setstkr
  F_MAX_STROKES_PH,
  F_MAX_STROKES_MV,
  // paramtr
  F_PASSCODE,
  F_FEEDING_DELAY,
  F_PRIORITY,          // 1=none, 2=pH, 3=Redox
  // allprbr
  F_PRBFAIL_PH_MODE,   // 0=DOSE, 1=STOP
  F_PRBFAIL_PH_MIN,    // 0 = disabled, 100-250 active
  F_PRBFAIL_MV_MODE,
  F_PRBFAIL_MV_MIN,
  F_COUNT,
};

struct GroupDef {
  const char *read_cmd;
  const char *write_cmd;   // nullptr = read-only group
  uint8_t first;           // first Field of the group
  uint8_t count;           // number of fields in the read reply
  uint8_t widths[12];      // fixed field widths of the write payload
};

static const GroupDef GROUPS[G_COUNT] = {
    {"valuer", nullptr, F_PH, 4, {0}},
    {"setpntr", "setpnw", F_PH_HIGH_SP, 12, {4, 4, 3, 3, 2, 1, 4, 4, 3, 3, 2, 1}},
    {"alldosr", "alldow", F_DOSAL_PH_MODE, 4, {1, 3, 1, 3}},
    {"allrmr", nullptr, F_ALARM_1, 8, {0}},
    {"outptr", nullptr, F_OUT1, 3, {0}},
    {"flowstr", "flowsw", F_FLOW_MODE, 2, {1, 2}},
    {"diginpr", "diginw", F_DI_STANDBY_NC, 3, {1, 1, 1}},
    {"servicr", nullptr, F_SERVICE_OFFSET, 2, {0}},
    {"setstkr", "setskw", F_MAX_STROKES_PH, 2, {3, 3}},
    // paramw carries one extra trailing field the read does not return: tau(2)
    {"paramtr", "paramw", F_PASSCODE, 3, {4, 2, 1}},
    {"allprbr", "allprw", F_PRBFAIL_PH_MODE, 4, {1, 3, 1, 3}},
};

inline Group group_of(Field f) {
  for (uint8_t g = 0; g < G_COUNT; g++) {
    if (f >= GROUPS[g].first && f < GROUPS[g].first + GROUPS[g].count) return (Group) g;
  }
  return G_COUNT;
}

struct WriteEvent {
  const char *cmd;
  bool ok;
};

struct EmecProtocol {
  // ---- configuration (set before the first tick) --------------------------
  std::string prefix = "34";
  std::string slave = "01";
  uint32_t hb_interval = 500;     // ms, 0 disables the heartbeat
  uint32_t prime_ms = 2000;       // heartbeat-only period before the first query
  uint32_t query_timeout = 1500;  // ms
  uint32_t write_timeout = 3000;  // ms, writes may wait for an EEPROM commit
  uint32_t rx_guard = 20;         // ms of bus silence required before any TX
  uint32_t tx_gap = 60;           // ms between two of our own frames
  uint8_t query_attempts = 2;
  uint8_t write_attempts = 2;
  uint8_t tau = 0;                // paramw trailing field (not readable)

  // ---- decoded state -------------------------------------------------------
  int values[F_COUNT] = {0};
  bool known[F_COUNT] = {false};
  bool updated[G_COUNT] = {false};   // owner clears after publishing
  bool force[G_COUNT] = {false};     // owner clears: re-publish even if unchanged
  bool online = false;               // last full poll cycle got at least one reply
  bool cycle_done = false;           // owner clears
  std::vector<WriteEvent> events;    // owner drains (write results, for logging)

  // ---- instrumentation -----------------------------------------------------
  uint32_t rx_bytes = 0, rx_lines = 0, tx_frames = 0, replies = 0, timeouts = 0;

  int value(Field f) const { return values[f]; }
  bool has(Field f) const { return known[f]; }
  bool busy() const { return txn_.active || op_active_ || !ops_.empty() || poll_running_; }

  // ---- public API ----------------------------------------------------------

  // Queue one full read cycle (all groups). No-op while a cycle is running.
  void request_poll() {
    if (poll_running_) return;
    poll_running_ = true;
    poll_idx_ = 0;
    cycle_ok_ = false;
  }

  // Queue a read-modify-write of one field. `value` is the raw on-wire integer
  // (pH setpoints are x100). Returns false if the field is not writable, the
  // value does not fit its fixed width, or the queue is full.
  bool write_field(Field f, int value) {
    Group g = group_of(f);
    if (g == G_COUNT || GROUPS[g].write_cmd == nullptr || value < 0) return false;
    uint8_t idx = f - GROUPS[g].first;
    if (f == F_PASSCODE && value > 9999) return false;
    if (f == F_FEEDING_DELAY && value > 60) return false;
    if (f == F_PRIORITY && (value < 1 || value > 3)) return false;
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", value);
    if (strlen(buf) > GROUPS[g].widths[idx]) return false;
    if (ops_.size() >= MAX_OPS) return false;
    Op op;
    op.kind = OP_FIELD;
    op.group = g;
    op.index = idx;
    op.value = buf;
    ops_.push_back(op);
    return true;
  }

  // resalw: clears the latched alarms, then re-reads the alarm flags.
  bool reset_alarms() { return push_raw_("resalw", " ", G_ALLRM); }

  // clockw: HH MM SS fmt(1=Europe 24h) ?(0) DD MM YY ?(0)  (15 chars)
  bool sync_clock(int hour, int minute, int second, int day, int month, int year) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d%02d%02d10%02d%02d%02d0", hour % 100, minute % 100, second % 100,
             day % 100, month % 100, year % 100);
    return push_raw_("clockw", buf, G_COUNT);
  }

  // Feed raw bytes read from the UART.
  void feed(const uint8_t *data, size_t len, uint32_t now) {
    if (len == 0) return;
    rx_.append((const char *) data, len);
    rx_bytes += len;
    last_rx_ = now;
    rx_seen_ = true;
    if (rx_.size() > RX_MAX) rx_.erase(0, rx_.size() - RX_MAX / 2);
  }

  // Run the state machine. `tx(const uint8_t *, size_t)` sends bytes on the bus.
  // Call it often (every loop); at most one frame is transmitted per call.
  template<typename TX> void tick(uint32_t now, TX &&tx) {
    if (!started_) {
      started_ = true;
      t0_ = now;
      last_hb_ = now - hb_interval;   // first heartbeat goes out immediately
      last_tx_ = now - tx_gap;
    }

    // 1) complete frames received so far
    std::string line;
    while (pop_line_(line)) {
      rx_lines++;
      handle_line_(line);
    }

    // 2) reply timeout -> retry or give up
    if (txn_.active && txn_.sent && (int32_t) (now - txn_.deadline) >= 0) {
      timeouts++;
      if (txn_.attempts > 1) {
        txn_.attempts--;
        txn_.sent = false;
      } else {
        finish_txn_(false, std::string());
      }
    }

    // 3) pick the next transaction
    if (!txn_.active) next_();

    // 4) transmit. Never talk over the pump: wait for the bus to be quiet.
    if (rx_seen_ && (uint32_t) (now - last_rx_) < rx_guard) return;
    if ((uint32_t) (now - last_tx_) < tx_gap) return;

    if (hb_interval != 0 && (uint32_t) (now - last_hb_) >= hb_interval) {
      std::string hb = prefix + "tb00 #0#\r\n";
      tx((const uint8_t *) hb.data(), hb.size());
      tx_frames++;
      last_hb_ = now;
      last_tx_ = now;
      return;
    }

    // Queries are sent right after a heartbeat, so the pump has the whole
    // remaining heartbeat period to answer and the 500 ms cadence is untouched.
    bool primed = (uint32_t) (now - t0_) >= prime_ms;
    bool early = hb_interval == 0 || (uint32_t) (now - last_hb_) < hb_interval / 2;
    if (txn_.active && !txn_.sent && primed && early) {
      rx_.clear();   // drop stale bytes; the reply we want comes after this frame
      tx((const uint8_t *) txn_.frame.data(), txn_.frame.size());
      tx_frames++;
      txn_.sent = true;
      txn_.deadline = now + txn_.timeout;
      last_tx_ = now;
    }
  }

 protected:
  static constexpr size_t RX_MAX = 2048;
  static constexpr size_t MAX_OPS = 16;

  enum OpKind : uint8_t { OP_FIELD, OP_RAW };
  struct Op {
    OpKind kind = OP_FIELD;
    Group group = G_COUNT;       // OP_FIELD: group written; OP_RAW: group re-read after (G_COUNT = none)
    uint8_t index = 0;           // OP_FIELD: field index inside the group
    std::string value;           // OP_FIELD: new field text; OP_RAW: full payload
    const char *cmd = nullptr;   // OP_RAW: write command
    uint8_t stage = 0;
  };
  struct Txn {
    bool active = false, sent = false, is_write = false;
    std::string frame, want;
    uint32_t deadline = 0, timeout = 0;
    uint8_t attempts = 0;
  };

  std::string rx_;
  uint32_t last_rx_ = 0, last_tx_ = 0, last_hb_ = 0, t0_ = 0;
  bool rx_seen_ = false, started_ = false;

  Txn txn_;
  std::deque<Op> ops_;
  Op op_;
  bool op_active_ = false;
  bool ctx_op_ = false;            // current transaction belongs to op_ (else to the poll)
  Group poll_group_ = G_VALUER;
  uint8_t poll_idx_ = G_COUNT;     // next group to poll; G_COUNT = idle
  bool cycle_ok_ = false;
  bool poll_running_ = false;

  std::vector<std::string> raw_[G_COUNT];   // last reply fields, kept as text for writes

  bool push_raw_(const char *cmd, const char *payload, Group refresh) {
    if (ops_.size() >= MAX_OPS) return false;
    Op op;
    op.kind = OP_RAW;
    op.group = refresh;
    op.index = 0;
    op.value = payload;
    op.cmd = cmd;
    ops_.push_back(op);
    return true;
  }

  // Any CR or LF ends a frame; empty frames are skipped.
  bool pop_line_(std::string &out) {
    for (;;) {
      size_t p = rx_.find_first_of("\r\n");
      if (p == std::string::npos) return false;
      out = rx_.substr(0, p);
      rx_.erase(0, p + 1);
      if (!out.empty()) return true;
    }
  }

  static bool to_int_(const std::string &s, int &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return false;
    while (*end == ' ') end++;
    if (*end != '\0') return false;
    out = (int) v;
    return true;
  }

  // "34gpd01&WD#a#b#c#<cmd>end" -> {a, b, c}
  static bool split_payload_(const std::string &rsp, std::vector<std::string> &out) {
    size_t p = rsp.find("&WD");
    if (p == std::string::npos || rsp.find("end") == std::string::npos) return false;
    std::string body = rsp.substr(p + 3);
    size_t tail = body.rfind('#');
    if (tail == std::string::npos) return false;
    body.resize(tail);
    out.clear();
    size_t start = 0;
    bool first = true;
    for (;;) {
      size_t h = body.find('#', start);
      std::string part = body.substr(start, h == std::string::npos ? std::string::npos : h - start);
      if (!first) out.push_back(part);   // text before the first '#' is not a field
      first = false;
      if (h == std::string::npos) break;
      start = h + 1;
    }
    return true;
  }

  bool decode_(Group g, const std::string &rsp) {
    std::vector<std::string> f;
    const GroupDef &d = GROUPS[g];
    if (!split_payload_(rsp, f) || f.size() < d.count) return false;
    f.resize(d.count);
    for (uint8_t i = 0; i < d.count; i++) {
      int v;
      if (to_int_(f[i], v)) {
        values[d.first + i] = v;
        known[d.first + i] = true;
      }
    }
    raw_[g] = f;
    updated[g] = true;
    return true;
  }

  void start_read_(Group g) {
    txn_ = Txn();
    txn_.active = true;
    txn_.frame = prefix + slave + GROUPS[g].read_cmd + "\r\r";
    txn_.want = std::string(GROUPS[g].read_cmd) + "end";
    txn_.timeout = query_timeout;
    txn_.attempts = query_attempts;
  }

  void start_write_(const char *cmd, const std::string &payload) {
    txn_ = Txn();
    txn_.active = true;
    txn_.is_write = true;
    txn_.frame = prefix + slave + cmd + payload + cmd + "end\r\r";
    txn_.want = std::string(cmd) + "okend";
    txn_.timeout = write_timeout;
    txn_.attempts = write_attempts;
  }

  void handle_line_(const std::string &t) {
    if (!txn_.active || !txn_.sent) return;
    if (!txn_.is_write) {
      if (t.find(txn_.want) != std::string::npos && t.find("gpd") != std::string::npos) finish_txn_(true, t);
      return;
    }
    if (t.find(txn_.want) != std::string::npos) {
      finish_txn_(true, t);
    } else if (t.find("no#") != std::string::npos && t.find("end") != std::string::npos) {
      finish_txn_(false, t);   // "#no#change..." = rejected, do not retry
    }
  }

  void finish_txn_(bool ok, const std::string &text) {
    txn_.active = false;
    if (ok) replies++;
    if (ctx_op_) {
      op_step_(ok, text);
      return;
    }
    if (ok && decode_(poll_group_, text)) cycle_ok_ = true;
    if (poll_idx_ >= G_COUNT) {
      online = cycle_ok_;
      cycle_done = true;
      poll_running_ = false;
    }
  }

  void next_() {
    if (op_active_) return;   // the running op starts its own transactions
    if (!ops_.empty()) {
      op_ = ops_.front();
      ops_.pop_front();
      op_active_ = true;
      ctx_op_ = true;
      if (op_.kind == OP_FIELD) {
        op_.stage = 1;
        start_read_(op_.group);
      } else {
        op_.stage = 2;
        start_write_(op_.cmd, op_.value);
      }
      return;
    }
    if (poll_idx_ < G_COUNT) {
      ctx_op_ = false;
      poll_group_ = (Group) poll_idx_++;
      start_read_(poll_group_);
    }
  }

  void end_op_() {
    op_active_ = false;
    ctx_op_ = false;
    if (op_.group < G_COUNT) force[op_.group] = true;
  }

  void fail_op_(const char *cmd) {
    events.push_back({cmd, false});
    end_op_();
  }

  bool build_payload_(std::string &payload) {
    const GroupDef &d = GROUPS[op_.group];
    std::vector<std::string> v = raw_[op_.group];
    if (v.size() != d.count) return false;
    v[op_.index] = op_.value;
    uint8_t widths[13];
    size_t n = d.count;
    memcpy(widths, d.widths, sizeof(d.widths));
    if (op_.group == G_PARAMT) {
      char t[8];
      snprintf(t, sizeof(t), "%02u", (unsigned) (tau % 100));
      v.push_back(t);
      widths[n++] = 2;
    }
    payload.clear();
    for (size_t i = 0; i < n; i++) {
      if (v[i].size() > widths[i]) return false;
      payload.append(widths[i] - v[i].size(), '0');
      payload += v[i];
    }
    return true;
  }

  void op_step_(bool ok, const std::string &text) {
    const char *wcmd = op_.kind == OP_FIELD ? GROUPS[op_.group].write_cmd : op_.cmd;
    switch (op_.stage) {
      case 1: {   // fresh read done (fall back on the last known reply)
        if (ok) decode_(op_.group, text);
        std::string payload;
        if (!build_payload_(payload)) {
          fail_op_(wcmd);
          return;
        }
        op_.stage = 2;
        start_write_(wcmd, payload);
        return;
      }
      case 2:     // write done
        events.push_back({wcmd, ok});
        if (op_.group < G_COUNT) {
          op_.stage = 3;
          start_read_(op_.group);   // read back what the pump really stored
          return;
        }
        end_op_();
        return;
      default:    // read-back done
        if (ok) decode_(op_.group, text);
        end_op_();
        return;
    }
  }
};

}  // namespace emec_core
