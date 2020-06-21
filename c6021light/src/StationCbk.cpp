#include "StationCbk.h"

#include "hal/PrintfAb.h"

#include "MarklinI2C/Messages/AccessoryMsg.h"
#include "RR32Can/Constants.h"

void AccessoryCbk::begin(hal::HalBase& hal) { this->hal = &hal; }

void AccessoryCbk::OnAccessoryPacket(RR32Can::TurnoutPacket& packet, bool response) {
  if (!response) {
    MYPRINTF(" Ignoring Accessory request packet");
    return;
  }

  MYPRINTF(" Got an Accessory packet!");

  if (hal == nullptr) {
    return;
  }

  if ((packet.locid & RR32Can::kMMAccessoryAddrStart) != RR32Can::kMMAccessoryAddrStart) {
    // Not an MM2 packet
    return;
  }

  uint16_t turnoutAddr = packet.locid & 0x03FF;
  if (turnoutAddr > 0xFF) {
    // Addr too large for the i2c bus.
    return;
  }

  // Convert to i2c confirmation packet
  MarklinI2C::Messages::AccessoryMsg i2cMsg = hal->prepareI2cMessage();

  i2cMsg.setTurnoutAddr(turnoutAddr);
  i2cMsg.setPower(packet.power);
  i2cMsg.setDirection(packet.position);

  hal->SendI2CMessage(i2cMsg);
}

void AccessoryCbk::setSystemState(bool onOff) {
  // When the system is stopped, turn on the LED.
  hal->led(!onOff);
}