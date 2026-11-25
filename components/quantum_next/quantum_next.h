#pragma once

#include "esphome/components/button/button.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "protocol.h"

namespace esphome {
namespace quantum_next {

enum class SelectKind : uint8_t { DIRECTION = 0, MODE = 1 };
enum class SwitchKind : uint8_t { BOOST = 0, DEBUG = 1 };

class QuantumNext;

class QuantumNextFan final : public fan::Fan, public Parented<QuantumNext> {
 public:
  QuantumNextFan() { this->speed = 1; }
  fan::FanTraits get_traits() override;

 protected:
  void control(const fan::FanCall &call) override;
};

class QuantumNextSelect final : public select::Select, public Parented<QuantumNext> {
 public:
  void set_kind(SelectKind kind) { this->kind_ = kind; }

 protected:
  void control(size_t index) override;

  SelectKind kind_{SelectKind::DIRECTION};
};

class QuantumNextSwitch final : public switch_::Switch, public Parented<QuantumNext> {
 public:
  void set_kind(SwitchKind kind) { this->kind_ = kind; }
  // Publish state even when it is already that value, so a refused boost press
  // does not stay on in the frontend.
  void acknowledge(bool state);

 protected:
  void write_state(bool state) override;

  SwitchKind kind_{SwitchKind::BOOST};
};

class QuantumNextButton final : public button::Button, public Parented<QuantumNext> {
 protected:
  void press_action() override;
};

class QuantumNext final : public Component,
                          public remote_base::RemoteReceiverListener,
                          public remote_base::RemoteTransmittable {
 public:
  void setup() override;
  void dump_config() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

  void set_fan(QuantumNextFan *fan) { this->fan_ = fan; }
  void set_direction_select(QuantumNextSelect *select) { this->direction_select_ = select; }
  void set_mode_select(QuantumNextSelect *select) { this->mode_select_ = select; }
  void set_boost_switch(QuantumNextSwitch *sw) { this->boost_switch_ = sw; }
  void set_debug_switch(QuantumNextSwitch *sw) { this->debug_switch_ = sw; }

  bool is_publishing() const { return this->publishing_; }

  void handle_fan(const fan::FanCall &call);
  void handle_direction(uint8_t direction);
  void handle_mode(bool comfort);
  // False when boost was requested while the fan is off.
  bool request_boost(bool on);
  void set_debug(bool on) { this->debug_ = on; }
  void handle_filter_reset();

 protected:
  void mirror_();
  void publish_boost_(bool on);
  void transmit_(uint8_t command);
  void save_();
  void apply_frame_(const QnDecoded &frame);
  void log_frame_(const char *dir, const QnDecoded &frame) const;

  QuantumNextFan *fan_{nullptr};
  QuantumNextSelect *direction_select_{nullptr};
  QuantumNextSelect *mode_select_{nullptr};
  QuantumNextSwitch *boost_switch_{nullptr};
  QuantumNextSwitch *debug_switch_{nullptr};

  bool power_{false};
  uint8_t speed_{1};
  uint8_t direction_{0};
  uint8_t comfort_{1};
  uint8_t toggle_{0};
  bool debug_{false};
  bool publishing_{false};

  uint32_t ignore_rx_until_{0};
  uint32_t last_rx_ms_{0};
  uint32_t last_rx_hash_{0};
  bool have_rx_hash_{false};

  ESPPreferenceObject pref_{};
};

}  // namespace quantum_next
}  // namespace esphome
