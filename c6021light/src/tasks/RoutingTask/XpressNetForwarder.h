#ifndef __TASKS__ROUTINGTASK__XPRESSNETFORWARDER_H__
#define __TASKS__ROUTINGTASK__XPRESSNETFORWARDER_H__

#include "RoutingForwarder.h"

#include "RR32Can/Locomotive.h"
#include "RR32Can/messages/Data.h"
#include "RR32Can/messages/Identifier.h"

#include "XpressNet/XpressNetMsg.h"

#include "DataModel.h"

namespace tasks {
namespace RoutingTask {

/*
 * \brief Class XpressNetForwarder
 */
class XpressNetForwarder final : public RoutingForwarder {
 public:
  void init(DataModel& dataModel) {
    this->dataModel_ = &dataModel;
  }

  void forwardLocoChange(const RR32Can::LocomotiveData& loco, LocoDiff_t& diff) override;
  void forward(const RR32Can::CanFrame& frame) override;
  bool MakeRR32CanMsg(const XpressNetMsg::XNetMsg& XnPacket, RR32Can::CanFrame& frame);

 private:
  DataModel* dataModel_ = nullptr;
};

}  // namespace RoutingTask
}  // namespace tasks

#endif  // __TASKS__ROUTINGTASK__XPRESSNETFORWARDER_H__
