// Off-target test of the protocol core against a small simulated WDPHRH.
//
//   g++ -std=c++17 -Wall -Wextra -I components/emec tests/test_protocol.cpp -o /tmp/emec_test && /tmp/emec_test
//
// The simulated pump mimics what was captured on the real bus: it answers
// "3401<cmd>\r\r" queries, ACKs writes with "<cmd>okend", goes silent when the
// heartbeat stops, and the bus echoes our own bytes back (half-duplex RS-485).

#include "emec_protocol.h"

#include <cassert>
#include <cstdio>
#include <map>

using namespace emec_core;

struct Pump {
  std::map<std::string, std::string> regs = {
      {"valuer", "#0723#0655#000#0#"},
      {"setpntr", "#0770#0730#040#000#00#0#0630#0670#050#000#00#0#"},
      {"alldosr", "#0#000#0#000#"},
      {"allrmr", "#0#1#0#0#0#0#0#1#"},
      {"outptr", "#1#0#0#"},
      {"flowstr", "#2#05#"},
      {"diginpr", "#0#0#1#"},
      {"servicr", "#0#1234#"},
      {"setstkr", "#180#120#"},
      {"paramtr", "#0000#05#1#"},
      {"allprbr", "#0#000#1#120#"},
  };
  std::map<std::string, std::string> writes;   // last payload per write command
  std::string in;
  uint32_t last_hb = 0;
  bool reject_writes = false;
  bool mute = false;
  int drop_next = 0;                           // swallow the next N queries
  uint32_t collisions = 0;
  uint32_t busy_until = 4294950000u;                    // pump is transmitting until then

  struct Pending { uint32_t at; std::string data; };
  std::vector<Pending> out;

  void reply(uint32_t now, const std::string &s) {
    out.push_back({now + 30, s});
    busy_until = now + 30 + 20;
  }

  // Bytes written by the master.
  void rx(uint32_t now, const uint8_t *b, size_t n) {
    if ((int32_t) (now - busy_until) < 0) collisions++;       // master talked over our reply
    in.append((const char *) b, n);
    for (;;) {
      size_t p = in.find_first_of("\r\n");
      if (p == std::string::npos) break;
      std::string line = in.substr(0, p);
      in.erase(0, p + 1);
      if (!line.empty()) handle(now, line);
    }
  }

  void handle(uint32_t now, const std::string &l) {
    if (l == "34tb00 #0#") {
      last_hb = now;
      return;
    }
    if (mute || now - last_hb > 1200) return;  // session lost without heartbeat
    if (l.rfind("3401", 0) != 0) return;
    std::string body = l.substr(4);
    if (regs.count(body)) {
      if (drop_next > 0) {
        drop_next--;
        return;
      }
      reply(now, "34gpd01&WD" + regs[body] + body + "end\r");
      return;
    }
    // write: <cmd><payload><cmd>end
    static const char *WR[] = {"setpnw", "alldow", "setskw", "paramw", "clockw",
                               "allprw", "flowsw", "diginw", "resalw"};
    for (const char *w : WR) {
      std::string cmd = w, tail = cmd + "end";
      if (body.rfind(cmd, 0) != 0 || body.size() < cmd.size() + tail.size()) continue;
      if (body.compare(body.size() - tail.size(), tail.size(), tail) != 0) continue;
      std::string payload = body.substr(cmd.size(), body.size() - cmd.size() - tail.size());
      if (reject_writes) {
        reply(now, "34gpd01&WD#no#changeend\r");
        return;
      }
      writes[cmd] = payload;
      apply(cmd, payload);
      reply(now, "34gpd01&" + cmd + "okend\r");
      return;
    }
  }

  static std::string join(const std::string &p, std::initializer_list<int> w) {
    std::string out = "#";
    size_t o = 0;
    for (int k : w) {
      out += p.substr(o, k) + "#";
      o += k;
    }
    return out;
  }
  void apply(const std::string &cmd, const std::string &p) {
    if (cmd == "setpnw") regs["setpntr"] = join(p, {4, 4, 3, 3, 2, 1, 4, 4, 3, 3, 2, 1});
    if (cmd == "alldow") regs["alldosr"] = join(p, {1, 3, 1, 3});
    if (cmd == "setskw") regs["setstkr"] = join(p, {3, 3});
    if (cmd == "flowsw") regs["flowstr"] = join(p, {1, 2});
    if (cmd == "diginw") regs["diginpr"] = join(p, {1, 1, 1});
    if (cmd == "allprw") regs["allprbr"] = join(p, {1, 3, 1, 3});
    if (cmd == "paramw") regs["paramtr"] = join(p, {4, 2, 1});
    if (cmd == "resalw") regs["allrmr"] = "#0#0#0#0#0#0#0#0#";
  }
};

struct Bench {
  EmecProtocol proto;
  Pump pump;
  uint32_t now = 4294960000u;   // start close to the millis() wrap on purpose
  std::vector<uint32_t> hb_times;

  void step() {
    // deliver due pump replies
    for (size_t i = 0; i < pump.out.size();) {
      if ((int32_t) (now - pump.out[i].at) >= 0) {
        proto.feed((const uint8_t *) pump.out[i].data.data(), pump.out[i].data.size(), now);
        pump.out.erase(pump.out.begin() + i);
      } else {
        i++;
      }
    }
    proto.tick(now, [this](const uint8_t *b, size_t n) {
      if (std::string((const char *) b, n) == "34tb00 #0#\r\n") hb_times.push_back(now);
      pump.rx(now, b, n);
      proto.feed(b, n, now);    // half-duplex echo of our own frame
    });
    now += 5;
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 5) step();
  }
  void settle() {
    for (int i = 0; i < 12000 && proto.busy(); i++) step();   // up to 60 s
    assert(!proto.busy());
  }
};

#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("FAIL line %d: %s\n", __LINE__, #c); \
      return 1; \
    } \
  } while (0)

int main() {
  Bench b;

  // --- priming: heartbeats only during the first 2 s -----------------------
  b.proto.request_poll();
  b.run(1900);
  CHECK(b.proto.replies == 0);
  CHECK(b.hb_times.size() >= 3);

  // --- full read cycle ------------------------------------------------------
  b.settle();
  CHECK(b.proto.cycle_done && b.proto.online);
  b.proto.cycle_done = false;
  CHECK(b.proto.value(F_PH) == 723 && b.proto.value(F_MV) == 655);
  CHECK(b.proto.value(F_PH_HIGH_SP) == 770 && b.proto.value(F_PH_LOW_SP) == 730);
  CHECK(b.proto.value(F_PH_HIGH_PCT) == 40 && b.proto.value(F_MV_LOW_SP) == 630);
  CHECK(b.proto.value(F_MV_HIGH_SP) == 670 && b.proto.value(F_MV_LOW_PCT) == 50);
  CHECK(b.proto.value(F_ALARM_2) == 1 && b.proto.value(F_ALARM_8) == 1 && b.proto.value(F_ALARM_1) == 0);
  CHECK(b.proto.value(F_OUT1) == 1 && b.proto.value(F_FLOW_MODE) == 2 && b.proto.value(F_FLOW_TIME) == 5);
  CHECK(b.proto.value(F_DI_MV_LEVEL_NC) == 1 && b.proto.value(F_SERVICE_HOURS) == 1234);
  CHECK(b.proto.value(F_MAX_STROKES_PH) == 180 && b.proto.value(F_MAX_STROKES_MV) == 120);
  CHECK(b.proto.value(F_FEEDING_DELAY) == 5 && b.proto.value(F_PRIORITY) == 1);
  CHECK(b.proto.value(F_PRBFAIL_MV_MODE) == 1 && b.proto.value(F_PRBFAIL_MV_MIN) == 120);
  for (int f = 0; f < F_COUNT; f++) CHECK(b.proto.has((Field) f));

  // --- setpoint write: must match the frame captured on the real bus --------
  //   3401setpnw0760073004000000006300670050000000setpnwend
  CHECK(b.proto.write_field(F_PH_HIGH_SP, 760));
  b.settle();
  CHECK(b.pump.writes["setpnw"] == "0760073004000000006300670050000000");
  CHECK(b.proto.events.size() == 1 && b.proto.events[0].ok);
  CHECK(std::string(b.proto.events[0].cmd) == "setpnw");
  CHECK(b.proto.value(F_PH_HIGH_SP) == 760 && b.proto.force[G_SETPNT]);
  b.proto.events.clear();

  // --- two back-to-back writes in the same group must not clobber each other
  CHECK(b.proto.write_field(F_MV_LOW_SP, 650));
  CHECK(b.proto.write_field(F_MV_MODE, 1));
  b.settle();
  CHECK(b.pump.writes["setpnw"] == "0760073004000000006500670050000001");
  b.proto.events.clear();

  // --- the other groups ------------------------------------------------------
  CHECK(b.proto.write_field(F_MAX_STROKES_MV, 90));
  CHECK(b.proto.write_field(F_FLOW_MODE, 1));
  CHECK(b.proto.write_field(F_DI_STANDBY_NC, 1));
  CHECK(b.proto.write_field(F_DOSAL_PH_MIN, 45));
  CHECK(b.proto.write_field(F_PRBFAIL_PH_MIN, 150));
  b.proto.tau = 7;
  CHECK(b.proto.write_field(F_FEEDING_DELAY, 12));
  b.settle();
  CHECK(b.pump.writes["setskw"] == "180090");
  CHECK(b.pump.writes["flowsw"] == "105");
  CHECK(b.pump.writes["diginw"] == "101");
  CHECK(b.pump.writes["alldow"] == "00450000");
  CHECK(b.pump.writes["allprw"] == "01501120");
  CHECK(b.pump.writes["paramw"] == "000012107");   // passcode feed prio tau
  CHECK(b.proto.events.size() == 6);
  for (auto &e : b.proto.events) CHECK(e.ok);
  b.proto.events.clear();

  // --- validation ------------------------------------------------------------
  CHECK(!b.proto.write_field(F_PH, 700));           // read-only group
  CHECK(!b.proto.write_field(F_PH_HIGH_PCT, 1000)); // exceeds width 3
  CHECK(!b.proto.write_field(F_PRIORITY, 0));
  CHECK(!b.proto.write_field(F_MV_LOW_SP, -5));

  // --- reset alarms + clock ----------------------------------------------------
  CHECK(b.proto.reset_alarms());
  CHECK(b.proto.sync_clock(9, 35, 7, 10, 10, 2026));
  b.settle();
  CHECK(b.pump.writes["resalw"] == " ");
  CHECK(b.pump.writes["clockw"] == "093507101010260");
  CHECK(b.proto.value(F_ALARM_2) == 0 && b.proto.value(F_ALARM_8) == 0);
  b.proto.events.clear();

  // --- rejected write: reported, no retry, value untouched ----------------------
  b.pump.reject_writes = true;
  uint32_t t0 = b.now;
  CHECK(b.proto.write_field(F_PH_LOW_SP, 700));
  b.settle();
  CHECK(b.proto.events.size() == 1 && !b.proto.events[0].ok);
  CHECK(b.proto.value(F_PH_LOW_SP) == 730);
  CHECK(b.now - t0 < 2500);
  b.pump.reject_writes = false;
  b.proto.events.clear();

  // --- one lost query is retried ---------------------------------------------------
  b.pump.drop_next = 1;
  b.proto.request_poll();
  b.settle();
  CHECK(b.proto.online && b.proto.timeouts == 1);
  b.proto.cycle_done = false;

  // --- silent pump: cycle ends offline, then recovers -----------------------------
  b.pump.mute = true;
  b.proto.request_poll();
  b.settle();
  CHECK(b.proto.cycle_done && !b.proto.online);
  b.proto.cycle_done = false;
  CHECK(b.proto.value(F_PH) == 723);   // last known values are kept (sticky)
  b.pump.mute = false;
  b.proto.request_poll();
  b.settle();
  CHECK(b.proto.online);

  // --- bus discipline -------------------------------------------------------------
  CHECK(b.pump.collisions == 0);
  uint32_t max_gap = 0;
  for (size_t i = 1; i < b.hb_times.size(); i++) {
    uint32_t g = b.hb_times[i] - b.hb_times[i - 1];
    if (g > max_gap) max_gap = g;
  }
  printf("heartbeats=%zu max_gap=%ums tx=%u replies=%u timeouts=%u\n", b.hb_times.size(), max_gap,
         b.proto.tx_frames, b.proto.replies, b.proto.timeouts);
  CHECK(max_gap <= 600);   // the pump drops the session without a steady heartbeat

  printf("ALL OK\n");
  return 0;
}
