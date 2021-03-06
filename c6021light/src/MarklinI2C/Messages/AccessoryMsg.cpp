#include "MarklinI2C/Messages/AccessoryMsg.h"

#include <cinttypes>

#include "RR32Can/StlAdapter.h"

namespace MarklinI2C {
namespace Messages {

uint8_t AccessoryMsg::getDecoderOutput() const {
  uint8_t lowerBits = data_ & kDataLowerAddrMask;
  lowerBits >>= 1;

  uint8_t upperBits = data_ & kDataUpperAddrMask;
  upperBits >>= 2;

  uint8_t addr = upperBits | lowerBits;
  return addr;
}

RR32Can::MachineTurnoutAddress AccessoryMsg::getTurnoutAddr() const {
  uint8_t addr = 0;
  addr |= getSender();
  addr <<= 4;
  addr |= getDecoderOutput();
  return RR32Can::MachineTurnoutAddress(addr);
}

void AccessoryMsg::print() const {
#ifdef ARDUINO
  // Actual output currently available on Arduino only.
  Serial.print('[');
  Serial.print(destination_, BIN);
  Serial.print(' ');
  Serial.print(source_, BIN);
  Serial.print(' ');
  Serial.print(data_, BIN);
  Serial.print(']');

  // Sender
  Serial.print(F(" Keyboard: "));
  Serial.print(getSender(), DEC);

  Serial.print(F(" Decoder: "));
  Serial.print(getDecoderOutput(), DEC);

  Serial.print(F(" (Turnout Addr: "));
  Serial.print(getTurnoutAddr(), DEC);

  Serial.print(F(") Direction: "));
  switch (getDirection()) {
    case kDataDirRed:
      Serial.print(F("RED  "));
      break;
    case kDataDirGreen:
      Serial.print(F("GREEN"));
      break;
    default:
      Serial.print(F("WTF"));
      break;
  }

  Serial.print(F(" Power: "));
  switch (getPower()) {
    case 0:
      Serial.print(F("OFF"));
      break;
    case 1:
      Serial.print(F("ON "));
      break;
    default:
      Serial.print(F("WTF"));
      break;
  }

  Serial.println();
#else
  const char* directionString;
  const char* powerString;

  switch (getDirection()) {
    case RR32Can::TurnoutDirection::RED:
      directionString = "RED  ";
      break;
    case RR32Can::TurnoutDirection::GREEN:
      directionString = "GREEN";
      break;
    default:
      directionString = "WTF  ";
      break;
  }

  switch (getPower()) {
    case 0:
      powerString = "OFF";
      break;
    case 1:
      powerString = "ON ";
      break;
    default:
      powerString = "WTF";
      break;
  }

  printf("[0x%x 0x%x 0x%x] - Keyboard: %i, Decoder: %i, (Turnout: %" PRId32
         ", Direction: %s, Power: %s)\n",
         destination_, source_, data_, getSender(), getDecoderOutput(), getTurnoutAddr().value(),
         directionString, powerString);
#endif
}

}  // namespace Messages
}  // namespace MarklinI2C
