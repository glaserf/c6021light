// KeyboardDemoSketch
// Demonstrates the i2c communication with a Marklin Keyboard.
// Sends responses to a Keyboard and dumps the received command to the serial console.

// Note that this sketch is not robust against other messages. Recieving engine control messages
// Will likely require a reboot of the uC.

extern "C" {
#include <stdio.h>
#include <string.h>
}

#include <hal/LibOpencm3Hal.h>

using Hal_t = hal::LibOpencm3Hal;

#include "ConsoleManager.h"
#include "DataModel.h"

extern "C" {
#include "microrl.h"
}

#include "RR32Can/StlAdapter.h"

#include "StationCbk.h"

#include "tasks/ConsoleTask/ConsoleTask.h"
#include "tasks/RoutingTask/RoutingTask.h"

#include "RR32Can/RR32Can.h"
#include "RR32Can_config.h"

#include "hal/stm32I2C.h"

#include <FreeRTOS.h>
#include "OsQueue.h"
#include "OsTask.h"
#include "timers.h"

// ******** Variables and Constans********
Hal_t halImpl;
AccessoryCbk accessoryCbk;

DataModel dataModel;
tasks::RoutingTask::RoutingTask routingTask;
freertossupport::StaticOsTask<tasks::RoutingTask::RoutingTask,
                              tasks::RoutingTask::RoutingTask::kStackSize>
    routingTaskBuffer;
tasks::ConsoleTask::ConsoleTask consoleTask;
freertossupport::StaticOsTask<tasks::ConsoleTask::ConsoleTask,
                              tasks::ConsoleTask::ConsoleTask::kStackSize>
    consoleTaskBuffer;

ConsoleManager console;
microrl_t microrl;

// ******** Code ********
extern "C" {
void vApplicationStackOverflowHook(xTaskHandle pxTask __attribute((unused)),
                                   portCHAR* pcTaskName __attribute((unused))) {
  for (;;) {
    __asm("bkpt 2");  // Loop forever here..
  }
}

void hard_fault_handler(void) {
  uint32_t cfsr = *(uint32_t*)0xE000ED28;
  uint16_t ufsr = *(uint16_t*)0xE000ED2A;
  uint8_t bfsr = *(uint8_t*)0xE000ED29;
  uint8_t mmfsr = *(uint8_t*)0xE000ED28;

  uint32_t hfsr = *(uint32_t*)0xE000ED2C;
  __asm("bkpt 1");
  (void)cfsr;
  (void)ufsr;
  (void)bfsr;
  (void)mmfsr;
  (void)hfsr;
}

void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
                                   StackType_t** ppxIdleTaskStackBuffer,
                                   uint32_t* pulIdleTaskStackSize) {
  static StackType_t idleTaskStack[configMINIMAL_STACK_SIZE];
  static StaticTask_t idleTaskTcb;
  *ppxIdleTaskTCBBuffer = &idleTaskTcb;
  *ppxIdleTaskStackBuffer = idleTaskStack;
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t** ppxTimerTaskTCBBuffer,
                                    StackType_t** ppxTimerTaskStackBuffer,
                                    uint32_t* pulTimerTaskStackSize) {
  static StackType_t timerTaskStack[configTIMER_TASK_STACK_DEPTH];
  static StaticTask_t timerTaskTcb;
  *ppxTimerTaskTCBBuffer = &timerTaskTcb;
  *ppxTimerTaskStackBuffer = timerTaskStack;
  *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

}  // extern "C"

void microrl_print_cbk(const char* s) { printf(s); }

int run_app_set_turnout_protocol(int argc, const char* const* argv) {
  static constexpr const char* text{": Set Turnout protocol to "};
  if (argc < 2) {
    printf("%s: Too few arguments (1 expected).\n", argv[0]);
    return -2;
  } else if (argc > 2) {
    printf("%s: Too many arguments (1 expected).\n", argv[0]);
    return -2;
  }

  if (strncasecmp(argv[1], MM2Name, strlen(MM2Name)) == 0) {
    dataModel.accessoryRailProtocol = RR32Can::RailProtocol::MM2;
    printf("%s%s'%s'.\n", argv[0], text, argv[1]);
  } else if (strncasecmp(argv[1], DCCName, strlen(DCCName)) == 0) {
    dataModel.accessoryRailProtocol = RR32Can::RailProtocol::DCC;
    printf("%s%s'%s'.\n", argv[0], text, argv[1]);
  } else if (strncasecmp(argv[1], SX1Name, strlen(SX1Name)) == 0) {
    dataModel.accessoryRailProtocol = RR32Can::RailProtocol::SX1;
    printf("%s%s'%s'.\n", argv[0], text, argv[1]);
  } else {
    printf("%s: Unknown rail protocol '%s'.\n", argv[0], argv[1]);
    return -3;
  }

  return 0;
}

int run_app_get_turnout_protocol(int argc, const char* const* argv) {
  if (argc > 1) {
    printf("%s: Too many arguments (0 expected).\n", argv[0]);
    return -2;
  }

  const char* turnoutProtocol = nullptr;

  switch (dataModel.accessoryRailProtocol) {
    case RR32Can::RailProtocol::MM1:
    case RR32Can::RailProtocol::MM2:
    case RR32Can::RailProtocol::MFX:
      turnoutProtocol = MM2Name;
      break;
    case RR32Can::RailProtocol::DCC:
      turnoutProtocol = DCCName;
      break;
    case RR32Can::RailProtocol::SX1:
    case RR32Can::RailProtocol::SX2:
      turnoutProtocol = SX1Name;
      break;
  }

  printf("%s: The current turnout protocol is %s.\n", argv[0], turnoutProtocol);

  return 0;
}

int run_app_save(int argc, const char* const* argv) {
  if (argc > 1) {
    printf("%s: Too many arguments (0 expected).\n", argv[0]);
    return -2;
  }

  halImpl.SaveConfig(dataModel);
  printf("%s: Configuration saved to flash.\n", argv[0]);

  return 0;
}

void setup() {
  // Setup I2C & CAN
  halImpl.begin(dataModel.myAddr, &console, routingTaskBuffer.getHandle());

  // Setup Serial
  printf("Connect6021Light Initializing...\n");

  // Load Configuration
  printf("Reading Configuration.\n");
  dataModel = halImpl.LoadConfig();
  routingTask.begin(dataModel, halImpl);

  // Tie callbacks together
  printf("Setting up callbacks.\n");
  accessoryCbk.begin(halImpl);

  RR32Can::Station::CallbackStruct callbacks;
  callbacks.tx = &halImpl;
  callbacks.accessory = &routingTask;
  callbacks.system = &accessoryCbk;
  RR32Can::RR32Can.begin(RR32CanUUID, callbacks);

  printf("Ready!\n");
  consoleTask.setup(&halImpl);
  console.begin();
}

void setupOsResources() {
  constexpr static const uint8_t canqueuesize = 10;

  static freertossupport::StaticOsQueue<hal::LibOpencm3Hal::CanQueueType::QueueElement,
                                        canqueuesize>
      canrxqbuffer;
  hal::LibOpencm3Hal::canrxq = canrxqbuffer;

  constexpr static const uint8_t i2cqueuesize = canqueuesize;

  static freertossupport::StaticOsQueue<hal::I2CQueueType::QueueElement, i2cqueuesize> i2crxqBuffer;
  hal::i2cRxQueue = i2crxqBuffer;

  static freertossupport::StaticOsQueue<hal::I2CQueueType::QueueElement, i2cqueuesize> i2ctxqBuffer;
  hal::i2cTxQueue = i2ctxqBuffer;

  static StaticTimer_t i2cWdgTimerBuffer;
  hal::i2cWdg = xTimerCreateStatic("I2CWdg", HAL_I2C_WDG_TIMEOUT, false, nullptr, hal::i2cWdgCbk,
                                   &i2cWdgTimerBuffer);
}

void setupOsTasks() {
  routingTaskBuffer.Create(routingTask, "RoutingTask", configMAX_PRIORITIES - 1);
  consoleTaskBuffer.Create(consoleTask, "ConsoleTask",
                           0);  // Lowest prio as this task will always run.
}

// Main function for non-arduino
int main(void) {
  setupOsResources();
  setupOsTasks();

  setup();

  vTaskStartScheduler();

  while (1) {
    ;
  }
  return 0;
}