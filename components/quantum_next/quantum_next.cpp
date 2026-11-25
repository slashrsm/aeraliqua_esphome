#include "quantum_next.h"

#include "esphome/core/controller_registry.h"
#include "esphome/core/log.h"

namespace esphome {
namespace quantum_next {

static constexpr uint32_t PREF_HASH = 0x514E5801;
static constexpr uint32_t CARRIER_HZ = 38000;
static constexpr uint32_t SEND_TIMES = 2;
static constexpr uint32_t SEND_WAIT_US = 50000;
static constexpr uint32_t ECHO_IGNORE_MS = 800;
static constexpr uint32_t REPEAT_WINDOW_MS = 300;

ESPHOME_LOG_TAG(TAG, "quantum_next");

struct SavedState {
  uint8_t power;
  uint8_t speed;
  uint8_t direction;
  uint8_t comfort;
  uint8_t toggle;
} __attribute__((packed));

fan::FanTraits QuantumNextFan::get_traits() { return fan::FanTraits(false, true, false, 5); }

void QuantumNextFan::control(const fan::FanCall &call) {
  auto *hub = this->get_parent();
  if (hub == nullptr || hub->is_publishing())
    return;
  hub->handle_fan(call);
}

void QuantumNextSelect::control(size_t index) {
  auto *hub = this->get_parent();
  if (hub == nullptr || hub->is_publishing())
    return;
  if (this->kind_ == SelectKind::DIRECTION)
    hub->handle_direction(static_cast<uint8_t>(index));
  else
    hub->handle_mode(index == 0);
}

void QuantumNextSwitch::acknowledge(bool state) {
  if (!this->has_state() || this->state != state) {
    this->publish_state(state);
    return;
  }
  this->state_callback_.call(this->state);
#if defined(USE_SWITCH) && defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_switch_update(this);
#endif
}

void QuantumNextSwitch::write_state(bool state) {
  auto *hub = this->get_parent();
  if (hub == nullptr || hub->is_publishing())
    return;
  if (this->kind_ == SwitchKind::DEBUG) {
    hub->set_debug(state);
    this->publish_state(state);
    return;
  }
  if (!hub->request_boost(state)) {
    this->acknowledge(false);
    return;
  }
  this->acknowledge(state);
}

void QuantumNextButton::press_action() {
  auto *hub = this->get_parent();
  if (hub == nullptr || hub->is_publishing())
    return;
  hub->handle_filter_reset();
}

void QuantumNext::setup() {
  this->disable_loop();

  // Flash, so a power cycle keeps the last speed, direction, mode, and toggle.
  this->pref_ = global_preferences->make_preference<SavedState>(PREF_HASH, true);
  SavedState saved{};
  if (this->pref_.load(&saved) && saved.power <= 1 && saved.speed >= 1 && saved.speed <= 5 && saved.direction <= 2 &&
      saved.comfort <= 1 && saved.toggle <= 1) {
    this->power_ = saved.power != 0;
    this->speed_ = saved.speed;
    this->direction_ = saved.direction;
    this->comfort_ = saved.comfort;
    this->toggle_ = saved.toggle;
  }

  this->mirror_();
  this->publish_boost_(false);
  if (this->debug_switch_ != nullptr)
    this->debug_switch_->restore();
}

void QuantumNext::dump_config() {
  ESP_LOGCONFIG(TAG, "Aerauliqa Quantum NEXT");
  ESP_LOGCONFIG(TAG, "  Power: %s", ONOFF(this->power_));
  ESP_LOGCONFIG(TAG, "  Speed: %u", this->speed_);
  ESP_LOGCONFIG(TAG, "  Direction: %s", qn_direction_name(this->direction_));
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->comfort_ ? "comfort" : "efficiency");
}

void QuantumNext::mirror_() {
  if (this->speed_ < 1 || this->speed_ > 5)
    this->speed_ = 1;
  bool was = this->publishing_;
  this->publishing_ = true;
  if (this->fan_ != nullptr) {
    this->fan_->state = this->power_;
    this->fan_->speed = this->speed_;
    this->fan_->publish_state();
  }
  if (this->direction_select_ != nullptr)
    this->direction_select_->publish_state(static_cast<size_t>(this->direction_));
  if (this->mode_select_ != nullptr)
    this->mode_select_->publish_state(this->comfort_ ? static_cast<size_t>(0) : static_cast<size_t>(1));
  this->publishing_ = was;
}

void QuantumNext::publish_boost_(bool on) {
  if (this->boost_switch_ == nullptr)
    return;
  bool was = this->publishing_;
  this->publishing_ = true;
  this->boost_switch_->publish_state(on);
  this->publishing_ = was;
}

void QuantumNext::save_() {
  SavedState saved;
  saved.power = this->power_ ? 1 : 0;
  saved.speed = this->speed_;
  saved.direction = this->direction_;
  saved.comfort = this->comfort_;
  saved.toggle = this->toggle_;
  this->pref_.save(&saved);
}

void QuantumNext::log_frame_(const char *dir, const QnDecoded &frame) const {
  if (!this->debug_)
    return;
  char line[120];
  qn_format(frame, line, sizeof(line));
  ESP_LOGI(TAG, "%s %s", dir, line);
}

void QuantumNext::transmit_(uint8_t command) {
  if (this->transmitter_ == nullptr) {
    ESP_LOGW(TAG, "No transmitter configured");
    return;
  }
  int32_t raw[80];
  int count = qn_build_raw(this->toggle_, this->speed_, this->direction_, this->comfort_, command, this->power_ ? 1 : 0,
                           raw, 80);
  QnDecoded shown{};
  bool parsed = count > 0 && qn_decode(raw, static_cast<size_t>(count), shown);

  this->ignore_rx_until_ = millis() + ECHO_IGNORE_MS;
  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(CARRIER_HZ);
  int i = 0;
  while (i < count) {
    uint32_t mark = 0;
    uint32_t space = 0;
    if (raw[i] > 0) {
      mark = static_cast<uint32_t>(raw[i]);
      i++;
    }
    if (i < count && raw[i] < 0) {
      space = static_cast<uint32_t>(-raw[i]);
      i++;
    }
    if (mark != 0 && space != 0)
      data->item(mark, space);
    else if (mark != 0)
      data->mark(mark);
    else if (space != 0)
      data->space(space);
  }
  call.set_send_times(SEND_TIMES);
  call.set_send_wait(SEND_WAIT_US);
  call.perform();

  this->toggle_ ^= 1;
  this->save_();
  if (parsed)
    this->log_frame_("TX", shown);
  else if (this->debug_)
    ESP_LOGW(TAG, "TX encode produced no frame");
}

void QuantumNext::handle_fan(const fan::FanCall &call) {
  if (this->publishing_)
    return;
  bool on = call.get_state().value_or(this->power_);
  uint8_t speed = this->speed_;
  if (call.get_speed().has_value()) {
    int requested = *call.get_speed();
    if (requested < 1)
      requested = 1;
    if (requested > 5)
      requested = 5;
    speed = static_cast<uint8_t>(requested);
  }
  bool was_on = this->power_;
  this->power_ = on;
  this->speed_ = speed;
  this->mirror_();
  // Boost cannot run while the unit is off. The off frame is command 0.
  if (!on)
    this->publish_boost_(false);
  // A speed change while the fan stays off is remembered and sent on the next on.
  if (!on && !was_on && !call.get_state().has_value())
    this->save_();
  else
    this->transmit_(0);
}

void QuantumNext::handle_direction(uint8_t direction) {
  if (this->publishing_ || direction > 2)
    return;
  this->direction_ = direction;
  this->mirror_();
  if (this->power_)
    this->transmit_(0);
  else
    this->save_();
}

void QuantumNext::handle_mode(bool comfort) {
  if (this->publishing_)
    return;
  this->comfort_ = comfort ? 1 : 0;
  this->mirror_();
  if (this->power_)
    this->transmit_(0);
  else
    this->save_();
}

bool QuantumNext::request_boost(bool on) {
  if (!this->power_) {
    if (on)
      ESP_LOGW(TAG, "Boost ignored while the fan is off");
    return !on;
  }
  this->transmit_(on ? 1 : 2);
  return true;
}

void QuantumNext::handle_filter_reset() {
  if (this->publishing_)
    return;
  if (!this->power_) {
    ESP_LOGW(TAG, "Filter reset ignored while the fan is off");
    return;
  }
  this->transmit_(7);
}

void QuantumNext::apply_frame_(const QnDecoded &frame) {
  this->comfort_ = frame.comfort ? 1 : 0;
  // The next press, including ours, uses the other phase.
  this->toggle_ = static_cast<uint8_t>(frame.toggle ^ 1);
  if (frame.power_on) {
    this->power_ = true;
    if (frame.speed >= 1 && frame.speed <= 5)
      this->speed_ = frame.speed;
    if (frame.direction <= 2)
      this->direction_ = frame.direction;
  } else {
    this->power_ = false;
  }
  this->mirror_();
  if (frame.command == 1 || frame.command == 2)
    this->publish_boost_(frame.command == 1);
  this->save_();
  this->log_frame_("RX", frame);
}

bool QuantumNext::on_receive(remote_base::RemoteReceiveData data) {
  const auto &raw = data.get_raw_data();
  QnDecoded frame;
  if (raw.empty() || !qn_decode(raw.data(), raw.size(), frame)) {
    if (this->debug_)
      ESP_LOGI(TAG, "RX undecoded n=%u", static_cast<unsigned>(raw.size()));
    return false;
  }
  if (this->ignore_rx_until_ != 0 && static_cast<int32_t>(this->ignore_rx_until_ - millis()) > 0) {
    if (this->debug_)
      ESP_LOGI(TAG, "RX echo");
    return true;
  }
  uint32_t hash = (static_cast<uint32_t>(frame.b0) << 24) | (static_cast<uint32_t>(frame.b1) << 16) |
                  (static_cast<uint32_t>(frame.b2) << 8) | frame.b3;
  uint32_t now = millis();
  if (this->have_rx_hash_ && hash == this->last_rx_hash_ && (now - this->last_rx_ms_) < REPEAT_WINDOW_MS) {
    if (this->debug_)
      ESP_LOGI(TAG, "RX repeat");
    return true;
  }
  this->have_rx_hash_ = true;
  this->last_rx_hash_ = hash;
  this->last_rx_ms_ = now;
  this->apply_frame_(frame);
  return true;
}

}  // namespace quantum_next
}  // namespace esphome
