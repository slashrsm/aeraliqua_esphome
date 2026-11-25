#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Quantum NEXT IR. Host-testable: this header uses no ESPHome types.
//
// 33 Manchester bits, MSB first, no leader. Half-bit 450 µs. Replay at 38 kHz.
// Logical 1 is mark then space. Logical 0 is space then mark. A 900 µs pulse is
// two half-bits at the same level.
//
// Frame: 1 start bit | byte0 | byte1 | 0x00 | command byte.
//
// Each byte stores a 4-bit value in the high nibble and a check in the low nibble.
// Even popcount repeats the nibble (0x9 -> 0x99). Odd popcount follows with the
// complement (0xE -> 0xE1, 0x1 -> 0x1E, 0x2 -> 0x2D, 0x8 -> 0x87, 0x7 -> 0x78).
//
// Byte 0 high nibble, MSB first: toggle, speed bit 2, speed bit 1, speed bit 0.
// Byte 1 high nibble: intake, extract, comfort, unused (left 0).
// Direction while on: alternate = intake+extract, extract only, or intake only.
// Off clears speed and both direction bits and keeps the comfort bit.
// Command nibble: 0 ordinary, 1 boost on, 2 boost off, 7 filter reset.
// Boost does not rewrite speed or direction. The unit applies speed 5 extract locally.
//
// Received pulses shorter than 250 µs are noise. Shorter than 700 µs is one
// half-bit, shorter than 2000 µs is two. A pulse of 2000 µs or longer is the gap
// after the frame, including the 10000 µs idle ESPHome appends. Skip it.
// A frame that ends on a mark is missing the final space: treat that leftover mark
// as a 1. A train that starts with a space is missing the leading mark: put one
// mark in front and decode again.
//
// Humidity control and free cooling are unit-side. The remote has no IR command
// for either of them.

namespace esphome {
namespace quantum_next {

static constexpr int32_t kHalfBitUs = 450;

struct QnDecoded {
  bool power_on;
  uint8_t speed;      // 1..5 when on, 0 when off
  uint8_t direction;  // 0 alternate, 1 extract, 2 intake, 3 neither
  uint8_t comfort;    // 1 comfort, 0 efficiency
  uint8_t toggle;
  uint8_t command;  // 0 none, 1 boost on, 2 boost off, 7 filter reset
  uint8_t b0, b1, b2, b3;
  char bits[40];
};

inline int qn_pop4(int n) {
  int pop = 0;
  for (int i = 0; i < 4; i++)
    pop += (n >> i) & 1;
  return pop;
}

inline int qn_encode_nibble(int n) {
  n &= 0xF;
  int lo = (qn_pop4(n) & 1) ? ((~n) & 0xF) : n;
  return (n << 4) | lo;
}

inline bool qn_decode_nibble(int byte, int &nibble) {
  int hi = (byte >> 4) & 0xF;
  int lo = byte & 0xF;
  int expect = (qn_pop4(hi) & 1) ? ((~hi) & 0xF) : hi;
  if (lo != expect)
    return false;
  nibble = hi;
  return true;
}

inline int qn_flag_nibble(int direction, int comfort, bool power_on) {
  int n = 0;
  if (power_on) {
    if (direction == 0)
      n = 0xC;
    else if (direction == 1)
      n = 0x4;
    else
      n = 0x8;
  }
  if (comfort)
    n |= 0x2;
  return n;
}

inline const char *qn_direction_name(uint8_t direction) {
  if (direction == 0)
    return "alternate";
  if (direction == 1)
    return "extract";
  if (direction == 2)
    return "intake";
  return "neither";
}

inline const char *qn_command_name(uint8_t command) {
  if (command == 0)
    return "none";
  if (command == 1)
    return "boost-on";
  if (command == 2)
    return "boost-off";
  if (command == 7)
    return "filter-reset";
  return "other";
}

inline void qn_format(const QnDecoded &d, char *out, int cap) {
  snprintf(out, cap, "%s s%d %s %s t%d cmd %s bytes %02X %02X %02X %02X", d.power_on ? "on" : "off", d.speed,
           qn_direction_name(d.direction), d.comfort ? "comfort" : "efficiency", d.toggle, qn_command_name(d.command),
           d.b0, d.b1, d.b2, d.b3);
}

inline int qn_build_raw(int toggle, int speed, int direction, int comfort, int command, int power_on, int32_t *raw,
                        int cap) {
  uint8_t bytes[4];
  int n0 = power_on ? (((toggle & 1) << 3) | (speed & 7)) : ((toggle & 1) << 3);
  bytes[0] = qn_encode_nibble(n0);
  bytes[1] = qn_encode_nibble(qn_flag_nibble(direction, comfort, power_on));
  bytes[2] = 0x00;
  bytes[3] = qn_encode_nibble(command & 0xF);

  int cells[66];
  int nc = 0;
  auto emit_bit = [&](int bit) {
    if (bit) {
      cells[nc++] = 1;
      cells[nc++] = 0;
    } else {
      cells[nc++] = 0;
      cells[nc++] = 1;
    }
  };
  emit_bit(1);
  for (int bi = 0; bi < 4; bi++) {
    for (int k = 7; k >= 0; k--)
      emit_bit((bytes[bi] >> k) & 1);
  }

  int nr = 0;
  int level = cells[0];
  int run = 1;
  for (int i = 1; i < nc; i++) {
    if (cells[i] == level) {
      run++;
      continue;
    }
    if (nr < cap)
      raw[nr++] = level ? run * kHalfBitUs : -run * kHalfBitUs;
    level = cells[i];
    run = 1;
  }
  if (nr < cap)
    raw[nr++] = level ? run * kHalfBitUs : -run * kHalfBitUs;
  return nr;
}

inline bool qn_parse_bits(const char *bits, int nb, QnDecoded &out) {
  if (nb != 33 || bits[0] != '1')
    return false;
  auto read_byte = [&](int start) {
    int v = 0;
    for (int i = 0; i < 8; i++)
      v = (v << 1) | (bits[start + i] == '1');
    return v;
  };
  int b0 = read_byte(1);
  int b1 = read_byte(9);
  int b2 = read_byte(17);
  int b3 = read_byte(25);
  int n0, n1, n2, n3;
  if (!qn_decode_nibble(b0, n0) || !qn_decode_nibble(b1, n1) || !qn_decode_nibble(b2, n2) ||
      !qn_decode_nibble(b3, n3))
    return false;
  out.b0 = b0;
  out.b1 = b1;
  out.b2 = b2;
  out.b3 = b3;
  out.toggle = (n0 >> 3) & 1;
  out.speed = n0 & 7;
  out.comfort = (n1 >> 1) & 1;
  int intake = (n1 >> 3) & 1;
  int extract = (n1 >> 2) & 1;
  if (intake && extract)
    out.direction = 0;
  else if (!intake && extract)
    out.direction = 1;
  else if (intake && !extract)
    out.direction = 2;
  else
    out.direction = 3;
  out.command = n3;
  out.power_on = out.speed >= 1 && out.speed <= 5 && out.direction != 3;
  if (!out.power_on)
    out.speed = 0;
  for (int i = 0; i < nb && i < 39; i++)
    out.bits[i] = bits[i];
  out.bits[nb < 39 ? nb : 39] = 0;
  return true;
}

inline bool qn_bits_from_cells(const uint8_t *cells, int nh, int start, char *bits, int &nb) {
  nb = 0;
  int i = start;
  while (i + 1 < nh && nb < 33) {
    if (cells[i] && !cells[i + 1])
      bits[nb++] = '1';
    else if (!cells[i] && cells[i + 1])
      bits[nb++] = '0';
    else
      return false;
    i += 2;
  }
  if (i < nh && cells[i] && i + 1 == nh && nb < 33)
    bits[nb++] = '1';
  bits[nb] = 0;
  return nb == 33 && bits[0] == '1';
}

inline bool qn_decode(const int32_t *data, size_t n, QnDecoded &out) {
  uint8_t cells[96];
  int nh = 0;
  for (size_t i = 0; i < n && nh < 96; i++) {
    int mag = data[i] >= 0 ? data[i] : -data[i];
    if (mag < 250 || mag >= 2000)
      continue;
    int units = mag < 700 ? 1 : 2;
    uint8_t level = data[i] > 0 ? 1 : 0;
    for (int u = 0; u < units && nh < 96; u++)
      cells[nh++] = level;
  }
  char bits[40];
  int nb = 0;
  if (qn_bits_from_cells(cells, nh, 0, bits, nb) && qn_parse_bits(bits, nb, out))
    return true;
  if (qn_bits_from_cells(cells, nh, 1, bits, nb) && qn_parse_bits(bits, nb, out))
    return true;
  if (nh > 0 && nh < 96 && cells[0] == 0) {
    uint8_t prefixed[96];
    prefixed[0] = 1;
    for (int i = 0; i < nh; i++)
      prefixed[i + 1] = cells[i];
    if (qn_bits_from_cells(prefixed, nh + 1, 0, bits, nb) && qn_parse_bits(bits, nb, out))
      return true;
  }
  return false;
}

}  // namespace quantum_next
}  // namespace esphome
