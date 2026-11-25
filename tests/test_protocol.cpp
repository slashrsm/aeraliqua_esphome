// Host checks for the Quantum NEXT codec. No ESPHome types.
//
// c++ -std=c++17 -Wall -Wextra -Werror -I components/quantum_next tests/test_protocol.cpp -o /tmp/test_protocol && /tmp/test_protocol

#include <cstdio>
#include <cstring>

#include "protocol.h"

using esphome::quantum_next::QnDecoded;
using esphome::quantum_next::kHalfBitUs;
using esphome::quantum_next::qn_decode;
using esphome::quantum_next::qn_decode_nibble;
using esphome::quantum_next::qn_encode_nibble;

namespace {

int g_failures = 0;

void fail(const char *name, const char *detail) {
  std::printf("FAIL %s: %s\n", name, detail);
  g_failures++;
}

void expect_true(const char *name, bool ok, const char *detail) {
  if (!ok)
    fail(name, detail);
}

void expect_eq(const char *name, int got, int want) {
  if (got != want) {
    char buf[80];
    std::snprintf(buf, sizeof(buf), "got %d (0x%X) want %d (0x%X)", got, got, want, want);
    fail(name, buf);
  }
}

int timings_from_bits(const char *bits, bool leftover_mark, int32_t *raw, int cap) {
  uint8_t cells[96];
  int nh = 0;
  for (const char *p = bits; *p != '\0'; p++) {
    if (*p == '1') {
      cells[nh++] = 1;
      cells[nh++] = 0;
    } else if (*p == '0') {
      cells[nh++] = 0;
      cells[nh++] = 1;
    } else {
      return -1;
    }
  }
  if (leftover_mark)
    cells[nh++] = 1;
  if (nh == 0)
    return 0;
  int nr = 0;
  int level = cells[0];
  int run = 1;
  for (int i = 1; i < nh; i++) {
    if (cells[i] == level) {
      run++;
      continue;
    }
    if (nr >= cap)
      return -1;
    raw[nr++] = level ? run * kHalfBitUs : -run * kHalfBitUs;
    level = cells[i];
    run = 1;
  }
  if (nr >= cap)
    return -1;
  raw[nr++] = level ? run * kHalfBitUs : -run * kHalfBitUs;
  return nr;
}

void expect_frame(const char *name, const QnDecoded &got, int power, int speed, int direction, int comfort,
                   int toggle, int command, int b0, int b1, int b2, int b3) {
  expect_eq(name, got.power_on ? 1 : 0, power);
  expect_eq(name, got.speed, speed);
  expect_eq(name, got.direction, direction);
  expect_eq(name, got.comfort, comfort);
  expect_eq(name, got.toggle, toggle);
  expect_eq(name, got.command, command);
  expect_eq(name, got.b0, b0);
  expect_eq(name, got.b1, b1);
  expect_eq(name, got.b2, b2);
  expect_eq(name, got.b3, b3);
}

void check_roundtrip(const char *name, int toggle, int speed, int direction, int comfort, int command, int power_on,
                      int b0, int b1, int b2, int b3, int expect_power, int expect_speed, int expect_direction) {
  int32_t raw[80];
  int count = esphome::quantum_next::qn_build_raw(toggle, speed, direction, comfort, command, power_on, raw, 80);
  expect_true(name, count > 8, "encoder produced no timings");
  QnDecoded got{};
  if (!qn_decode(raw, static_cast<size_t>(count), got)) {
    fail(name, "round trip did not decode");
    return;
  }
  expect_frame(name, got, expect_power, expect_speed, expect_direction, comfort ? 1 : 0, toggle & 1, command & 0xF, b0,
               b1, b2, b3);
}

void check_nibble(int nibble, int byte) {
  char name[32];
  std::snprintf(name, sizeof(name), "nibble %X", nibble);
  expect_eq(name, qn_encode_nibble(nibble), byte);
  int decoded = -1;
  expect_true(name, qn_decode_nibble(byte, decoded), "check nibble rejected");
  expect_eq(name, decoded, nibble & 0xF);
}

}  // namespace

int main() {
  check_nibble(0x9, 0x99);
  check_nibble(0xE, 0xE1);
  check_nibble(0x1, 0x1E);
  check_nibble(0x2, 0x2D);
  check_nibble(0x8, 0x87);
  check_nibble(0x7, 0x78);
  check_nibble(0x0, 0x00);
  int bad = 0;
  expect_true("bad check", !qn_decode_nibble(0x9A, bad), "0x9A was accepted");

  // speed, direction (0 alt, 1 extract, 2 intake), comfort, toggle, command, power.
  check_roundtrip("s1 alt comfort t1", 1, 1, 0, 1, 0, 1, 0x99, 0xE1, 0x00, 0x00, 1, 1, 0);
  check_roundtrip("s2 alt comfort t0", 0, 2, 0, 1, 0, 1, 0x2D, 0xE1, 0x00, 0x00, 1, 2, 0);
  check_roundtrip("s2 alt comfort t1", 1, 2, 0, 1, 0, 1, 0xAA, 0xE1, 0x00, 0x00, 1, 2, 0);
  check_roundtrip("boost on s3 alt comfort t0", 0, 3, 0, 1, 1, 1, 0x33, 0xE1, 0x00, 0x1E, 1, 3, 0);
  check_roundtrip("boost off s3 alt comfort t1", 1, 3, 0, 1, 2, 1, 0xB4, 0xE1, 0x00, 0x2D, 1, 3, 0);
  check_roundtrip("filter reset s4 alt comfort t1", 1, 4, 0, 1, 7, 1, 0xCC, 0xE1, 0x00, 0x78, 1, 4, 0);
  // Off clears speed and direction even if the caller still remembers them.
  check_roundtrip("off comfort t0", 0, 5, 2, 1, 0, 0, 0x00, 0x2D, 0x00, 0x00, 0, 0, 3);
  check_roundtrip("off efficiency t1", 1, 4, 1, 0, 0, 0, 0x87, 0x00, 0x00, 0x00, 0, 0, 3);

  int32_t raw[80];
  int count = esphome::quantum_next::qn_build_raw(1, 3, 0, 1, 2, 1, raw, 80);
  expect_true("boost off tail", count > 2 && raw[count - 1] < 0, "frame did not end on a space");
  QnDecoded dropped{};
  if (!qn_decode(raw, static_cast<size_t>(count - 1), dropped))
    fail("boost off tail", "dropping the final space did not decode");
  else
    expect_frame("boost off tail", dropped, 1, 3, 0, 1, 1, 2, 0xB4, 0xE1, 0x00, 0x2D);

  // 32 bits logged from the unit, plus the leftover mark of the swallowed final space.
  const char *capture = "11011010011100001000000000010110";
  expect_eq("capture length", static_cast<int>(std::strlen(capture)), 32);
  int32_t captured[80];
  int captured_n = timings_from_bits(capture, true, captured, 80);
  QnDecoded from_capture{};
  if (captured_n < 0 || !qn_decode(captured, static_cast<size_t>(captured_n), from_capture))
    fail("32-bit capture", "did not decode");
  else
    expect_frame("32-bit capture", from_capture, 1, 3, 0, 1, 1, 2, 0xB4, 0xE1, 0x00, 0x2D);

  count = esphome::quantum_next::qn_build_raw(1, 1, 0, 1, 0, 1, raw, 80);
  expect_true("clipped lead", count > 2 && raw[0] > 0, "frame did not start with a mark");
  QnDecoded clipped{};
  if (!qn_decode(raw + 1, static_cast<size_t>(count - 1), clipped))
    fail("clipped lead", "missing leading mark did not decode");
  else
    expect_frame("clipped lead", clipped, 1, 1, 0, 1, 1, 0, 0x99, 0xE1, 0x00, 0x00);

  raw[count++] = -10000;
  QnDecoded gapped{};
  if (!qn_decode(raw, static_cast<size_t>(count), gapped))
    fail("idle gap", "10000 us gap aborted the frame");
  else
    expect_eq("idle gap", gapped.b0, 0x99);

  int32_t noise[] = {100, -100, 100};
  QnDecoded ignored{};
  expect_true("noise", !qn_decode(noise, 3, ignored), "noise decoded as a frame");
  expect_true("empty", !qn_decode(nullptr, 0, ignored), "empty train decoded");

  if (g_failures == 0)
    std::printf("ok\n");
  else
    std::printf("%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
