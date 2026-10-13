// Off-target test of the pH / Redox spike filter.
//
//   g++ -std=c++17 -Wall -Wextra -I components/emec tests/test_filter.cpp -o /tmp/emec_filter && /tmp/emec_filter

#include "emec_filter.h"

#include <cstdio>

using emec_core::OutlierFilter;

#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("FAIL line %d: %s\n", __LINE__, #c); \
      return 1; \
    } \
  } while (0)

static const auto P = OutlierFilter::PUBLISH;
static const auto H = OutlierFilter::HOLD;

int main() {
  // pH, default settings: +/-1.00 (raw 100), alarm after 3 off readings
  OutlierFilter ph;
  ph.threshold = 100;
  ph.max_outliers = 3;

  CHECK(ph.feed(720) == P);           // first reading is trusted
  CHECK(ph.feed(740) == P);           // small move: followed
  CHECK(ph.feed(840) == P);           // exactly +1.00: still normal
  CHECK(ph.reference == 840);

  // isolated spike: dropped, never published
  CHECK(ph.feed(140) == H);
  CHECK(ph.feed(845) == P && !ph.alarm && ph.outliers == 0);

  // 3 off readings then back: dropped, no alarm
  CHECK(ph.feed(1300) == H);
  CHECK(ph.feed(1310) == H);
  CHECK(ph.feed(1305) == H);
  CHECK(ph.feed(850) == P && !ph.alarm);
  CHECK(ph.rejected == 4);

  // persistent deviation: 3 held, the 4th raises the alarm and is published
  CHECK(ph.feed(500) == H);
  CHECK(ph.feed(505) == H);
  CHECK(ph.feed(500) == H);
  CHECK(ph.feed(498) == P && ph.alarm);
  CHECK(ph.feed(495) == P && ph.alarm);   // stays published while in alarm
  CHECK(ph.reference == 850);             // reference kept at the last normal value

  // back within threshold of the last normal value: alarm clears
  CHECK(ph.feed(760) == P && !ph.alarm && ph.reference == 760);

  // reset (e.g. after a calibration): the current level becomes the reference
  CHECK(ph.feed(500) == H);
  ph.reset();
  CHECK(ph.feed(500) == P && !ph.alarm && ph.reference == 500);
  CHECK(ph.feed(520) == P);

  // Redox, +/-50 mV, alarm after 4 off readings
  OutlierFilter mv;
  mv.threshold = 50;
  mv.max_outliers = 4;
  CHECK(mv.feed(650) == P);
  CHECK(mv.feed(700) == P);               // +50: normal
  for (int i = 0; i < 4; i++) CHECK(mv.feed(800) == H);
  CHECK(mv.feed(800) == P && mv.alarm);

  // disabled filter: everything goes through
  OutlierFilter off;
  CHECK(off.feed(0) == P && off.feed(1400) == P && off.feed(0) == P && !off.alarm);

  printf("ALL OK\n");
  return 0;
}
