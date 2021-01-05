#include "tasks/RoutingTask/RoutingTask.h"

#include <cstdio>

#include <LocoNet.h>
#include "RR32Can/RR32Can.h"
#include "hal/stm32I2C.h"
#include "hal/stm32can.h"
#include "tasks/RoutingTask/LocoNetPrinter.h"

#include "DataModel.h"

namespace tasks {
namespace RoutingTask {

/**
 * \brief When a message was received, create and send a response message.
 */
void RoutingTask::TaskMain() {
  while (1) {
    waitForNotify();

    // Process CAN
    for (auto receiveResult = hal::getCanMessage(); receiveResult.messageValid;
         receiveResult = hal::getCanMessage()) {
      i2cForwarder_.forward(receiveResult.msg);
      lnForwarder_.forward(receiveResult.msg);
      // Forward to self
      RR32Can::RR32Can.HandlePacket(receiveResult.msg);

      // Attempt to forward all updates in the CAN DB
      if (!slotServer_.isDisabled()) {
        for (auto& locoEntry : engineDb_) {
          if (locoEntry.hasUpdate()) {
            lnForwarder_.forwardLocoChange(locoEntry.loco, locoEntry.diff);
          }
        }
      }
    }

    // Process I2C
    for (auto receiveResult = hal::getI2CMessage(); receiveResult.messageValid;
         receiveResult = hal::getI2CMessage()) {
      MarklinI2C::Messages::AccessoryMsg& request = receiveResult.msg;
      printf("I2C RX: ");
      request.print();

      RR32Can::CanFrame frame;

      // Convert to generic CAN representation
      if (i2cForwarder_.MakeRR32CanMsg(request, frame)) {
        RR32Can::RR32Can.SendPacket(frame);
        lnForwarder_.forward(frame);
        // Forward to self
        RR32Can::RR32Can.HandlePacket(frame);
      }
    }

    // Process LocoNet
    for (lnMsg* LnPacket = LocoNet.receive(); LnPacket; LnPacket = LocoNet.receive()) {
      printLnPacket(*LnPacket);

      RR32Can::CanFrame frame;

      // Convert to generic CAN representation
      if (lnForwarder_.MakeRR32CanMsg(*LnPacket, frame)) {
        i2cForwarder_.forward(frame);
        // Forward to CAN
        RR32Can::RR32Can.SendPacket(frame);

        // Forward to self
        RR32Can::RR32Can.HandlePacket(frame);
      }
      slotServer_.process(*LnPacket);

      if (!slotServer_.isDisabled()) {
        for (auto& locoEntry : slotServer_) {
          // See if the engine needs a match to CAN
          if (locoEntry.needsMatchToCAN) {
            const auto enginePtr = engineDb_.findEngine(locoEntry.loco.getAddress());
            if (enginePtr != nullptr) {
              locoEntry.loco.setUid(enginePtr->getUid());
            }
          }

          // Attempt to forward all updates in the Slot server
          if (locoEntry.hasUpdate()) {
            canForwarder_.forwardLocoChange(locoEntry.loco, locoEntry.diff);
          }
        }
      }
    }
  }
}

}  // namespace RoutingTask
}  // namespace tasks