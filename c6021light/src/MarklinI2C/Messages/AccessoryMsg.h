#ifndef __MARKLINI2C__MESSAGES__ACCESSORYMSG_H__
#define __MARKLINI2C__MESSAGES__ACCESSORYMSG_H__

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#endif

#include "MarklinI2C/Constants.h"

namespace MarklinI2C {
namespace Messages {

/**
 * \brief Class representing an accessory switch packet on I2C.
 */
class AccessoryMsg {
 public:
  constexpr static const uint8_t kAccesoryMessageBytes = 3;

  AccessoryMsg(){};

  AccessoryMsg(const AccessoryMsg&) = default;
  AccessoryMsg& operator=(const AccessoryMsg&) = default;

  AccessoryMsg(AccessoryMsg&&) = default;
  AccessoryMsg& operator=(AccessoryMsg&&) = default;

  AccessoryMsg(uint8_t destination, uint8_t source, uint8_t data)
      : destination_(destination), source_(source), data_(data){};

  uint8_t getSender() const { return (source_ & 0b00011110) >> 1; }

  /// Obtian the de-masked decoder output address.
  uint8_t getDecoderOutput() const;

  /**
   * \brief Obtain the complete turnout address.
   *
   * The result is 0-based. Pressing a button for Turnout 1
   * on the first Keyboard is Address 0.
   */
  uint8_t getTurnoutAddr() const;

  uint8_t getDirection() const { return data_ & kDataDirMask; }
  uint8_t getPower() const { return (data_ & kDataPowerMask) >> 3; }

  void setDirection(uint8_t direction) {
    if (direction == 0) {
      this->data_ &= 0xFE;
    } else {
      this->data_ |= 1;
    }
  }

  void setPower(uint8_t power) {
    power <<= 3;
    power &= kDataPowerMask;
    this->data_ |= power;
  }

  void setTurnoutAddr(uint8_t addr);

  void print() const;

  /**
   * \brief Assume that this message is a request message from a keyboard and
   * create a response message for the keyboard.
   */
  AccessoryMsg makeResponse() const;

  uint8_t destination_ = 0;
  uint8_t source_ = 0;
  uint8_t data_ = 0;
};

}  // namespace Messages
}  // namespace MarklinI2C

#endif  // __MARKLINI2C__MESSAGES__ACCESSORYMSG_H__
