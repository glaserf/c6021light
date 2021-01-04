#include "tasks/RoutingTask/LocoNetForwarder.h"

#include "RR32Can/Constants.h"
#include "RR32Can/messages/S88Event.h"
#include "RR32Can/messages/SystemMessage.h"
#include "RR32Can/messages/TurnoutPacket.h"

#include "LocoNet.h"

namespace tasks {
namespace RoutingTask {

void LocoNetForwarder::forward(const RR32Can::Identifier rr32id, const RR32Can::Data& rr32data) {
  switch (rr32id.getCommand()) {
    case RR32Can::Command::ACCESSORY_SWITCH: {
      const RR32Can::TurnoutPacket turnoutPacket(const_cast<RR32Can::Data&>(rr32data));
      if (!rr32id.isResponse()) {
        // Send to LocoNet
        LocoNet.requestSwitch(
            RR32Can::HumanTurnoutAddress(turnoutPacket.getLocid().getNumericAddress()).value(),
            turnoutPacket.getPower(),
            RR32Can::TurnoutDirectionToIntegral(turnoutPacket.getDirection()));
      }
      break;
    }

    case RR32Can::Command::SYSTEM_COMMAND: {
      const RR32Can::SystemMessage systemMessage(const_cast<RR32Can::Data&>(rr32data));
      switch (systemMessage.getSubcommand()) {
        case RR32Can::SystemSubcommand::SYSTEM_STOP:
          LocoNet.reportPower(false);
          break;
        case RR32Can::SystemSubcommand::SYSTEM_GO:
          LocoNet.reportPower(true);
          break;
        default:
          // Other messages not forwarded.
          break;
      }
      break;
    }
    case RR32Can::Command::S88_EVENT: {
      const RR32Can::S88Event s88Event(const_cast<RR32Can::Data&>(rr32data));
      if (s88Event.getSubtype() == RR32Can::S88Event::Subtype::RESPONSE) {
        uint8_t state = 0;
        switch (s88Event.getNewState()) {
          case RR32Can::S88Event::State::OPEN:
            state = 0;
            break;
          case RR32Can::S88Event::State::CLOSED:
            state = 1;
            break;
        }
        LocoNet.reportSensor(RR32Can::HumanTurnoutAddress(s88Event.getContactId()).value(), state);
      }
      break;
    }
    default:
      // Other messages not forwarded.
      break;
  }
}

void LocoNetForwarder::forwardLocoChange(
    const RR32Can::LocomotiveData& loco, const bool velocityChange, const bool directionChange,
    const RR32Can::LocomotiveData::FunctionBits_t functionChanges) {
  // In passive mode:

  // Get a hold of the slot server
  // Find if there is a slot known for the loco
  // If so, send messages for that slot.

  // Future TODO: If loco is not known, remember the request in some queue and send out a request
  // for the Address. Associate a timeout and clean the entry from the queue if the timeout is
  // exceeded.

  // In active mode:
  // Allocate a slot for the loco and start sending packets immediately.

  (void)loco;
  (void)velocityChange;
  (void)directionChange;
  (void)functionChanges;
  // To be implemented
}

bool LocoNetForwarder::MakeRR32CanMsg(const lnMsg& LnPacket, RR32Can::Identifier& rr32id,
                                      RR32Can::Data& rr32data) {
  // Decode the opcode
  switch (LnPacket.data[0]) {
    case OPC_SW_REQ: {
      rr32id.setCommand(RR32Can::Command::ACCESSORY_SWITCH);
      rr32id.setResponse(false);
      RR32Can::TurnoutPacket turnoutPacket(rr32data);
      turnoutPacket.initData();

      // Extract the switch address
      RR32Can::MachineTurnoutAddress lnAddr{static_cast<RR32Can::MachineTurnoutAddress::value_type>(
          ((LnPacket.srq.sw2 & 0x0F) << 7) | LnPacket.srq.sw1)};
      lnAddr.setProtocol(dataModel_->accessoryRailProtocol);
      turnoutPacket.setLocid(lnAddr);

      RR32Can::TurnoutDirection direction =
          ((LnPacket.srq.sw2 & OPC_SW_REQ_DIR) == 0 ? RR32Can::TurnoutDirection::RED
                                                    : RR32Can::TurnoutDirection::GREEN);
      turnoutPacket.setDirection(direction);
      uint8_t power = LnPacket.srq.sw2 & OPC_SW_REQ_OUT;
      turnoutPacket.setPower(power);

      return true;
      break;
    }
    case OPC_GPON:
    case OPC_GPOFF: {
      rr32id.setCommand(RR32Can::Command::SYSTEM_COMMAND);
      rr32id.setResponse(false);
      RR32Can::SystemMessage systemMessage(rr32data);
      systemMessage.initData();

      if (LnPacket.data[0] == OPC_GPON) {
        systemMessage.setSubcommand(RR32Can::SystemSubcommand::SYSTEM_GO);
      } else {
        systemMessage.setSubcommand(RR32Can::SystemSubcommand::SYSTEM_STOP);
      }

      return true;
      break;
    }
    case OPC_INPUT_REP: {
      rr32id.setCommand(RR32Can::Command::S88_EVENT);
      rr32id.setResponse(true);
      RR32Can::S88Event message(rr32data);
      message.initData();
      message.setSubtype(RR32Can::S88Event::Subtype::RESPONSE);
      message.setDeviceId(RR32Can::MachineTurnoutAddress(0));

      {
        uint16_t addr = LnPacket.ir.in1 << 1;
        addr |= (LnPacket.ir.in2 & 0x0F) << 8;
        addr |= (LnPacket.ir.in2 & 0x20) >> 5;
        RR32Can::MachineTurnoutAddress lnAddr(addr);

        message.setContactId(lnAddr);
      }
      {
        RR32Can::S88Event::State newState;
        RR32Can::S88Event::State oldState;
        if (((LnPacket.ir.in2 & 0x10) >> 4) == 0) {
          newState = RR32Can::S88Event::State::OPEN;
          oldState = RR32Can::S88Event::State::CLOSED;
        } else {
          newState = RR32Can::S88Event::State::CLOSED;
          oldState = RR32Can::S88Event::State::OPEN;
        }

        message.setStates(oldState, newState);
      }
      message.setTime(0);
      return true;
      break;
    }
    default:
      // Other packet types not handled.
      return false;
      break;
  }
}

}  // namespace RoutingTask
}  // namespace tasks
