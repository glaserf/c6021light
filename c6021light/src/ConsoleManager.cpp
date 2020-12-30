#include "ConsoleManager.h"

#include <cstdio>
#include <cstring>

#include "RR32Can/Constants.h"
#include "cliSupport.h"

#include "hal/stm32eepromEmulation.h"

#define NUM_COMPLETIONS (6)

#define COMMAND(cmdName) command_##cmdName

#define DEFINE_COMMAND_STRING(cmdName) \
  static constexpr const char* COMMAND(cmdName) { #cmdName }

#define COMMAND_ARGS(cmdName) args_##cmdName

namespace ConsoleManager {

DataModel* dataModel_;
microrl_t microrl;

DEFINE_COMMAND_STRING(config);

DEFINE_COMMAND_STRING(get);
DEFINE_COMMAND_STRING(set);
DEFINE_COMMAND_STRING(active);
DEFINE_COMMAND_STRING(passive);
DEFINE_COMMAND_STRING(disabled);

DEFINE_COMMAND_STRING(turnoutProtocol);

DEFINE_COMMAND_STRING(lnSlotServer);

DEFINE_COMMAND_STRING(flash);
DEFINE_COMMAND_STRING(dump);
DEFINE_COMMAND_STRING(save);
DEFINE_COMMAND_STRING(help);

static constexpr const char* turnoutProtocolHelp{
    "Data Protocol used for turnout commands generated by I2C or LocoNet."};
static constexpr const char* lnSlotServerHelp{"Slot Server for supporting throttles on LocoNet."};

int run_app_set_turnout_protocol(int argc, const char* const* argv, int argcMatched);
int run_app_get_turnout_protocol(int argc, const char* const* argv, int argcMatched);
int run_app_save(int argc, const char* const* argv, int argcMatched);
int run_app_help(int argc, const char* const* argv, int argcMatched);
void display_help(int argc, const char* const* argv);
int run_app_dump_flash(int argc, const char* const* argv,
                       int argcMatched);  // implemented in eeprom emulation
int run_ln_slot_server_active(int argc, const char* const* argv, int argcMatched);
int run_ln_slot_server_passive(int argc, const char* const* argv, int argcMatched);
int run_ln_slot_server_disable(int argc, const char* const* argv, int argcMatched);
int run_ln_slot_server_getstatus(int argc, const char* const* argv, int argcMatched);

// Arguments for config
static const cliSupport::Argument turnoutProtocolArguments[] = {
    {MM2Name, nullptr, nullptr}, {DCCName, nullptr, nullptr}, {SX1Name, nullptr, nullptr}, {}};

static const cliSupport::Argument lnSlotServerArguments[] = {
    {COMMAND(active), nullptr, run_ln_slot_server_active},
    {COMMAND(passive), nullptr, run_ln_slot_server_passive},
    {COMMAND(disabled), nullptr, run_ln_slot_server_disable},
    {}};

static const cliSupport::Argument COMMAND_ARGS(config_get)[] = {
    {COMMAND(turnoutProtocol), nullptr, run_app_get_turnout_protocol, turnoutProtocolHelp},
    {COMMAND(lnSlotServer), nullptr, run_ln_slot_server_getstatus, lnSlotServerHelp},
    {}};

static const cliSupport::Argument COMMAND_ARGS(config_set)[] = {
    {COMMAND(turnoutProtocol), turnoutProtocolArguments, run_app_set_turnout_protocol,
     turnoutProtocolHelp},
    {COMMAND(lnSlotServer), lnSlotServerArguments, nullptr, lnSlotServerHelp},
    {}};

static const cliSupport::Argument COMMAND_ARGS(config)[] = {
    {COMMAND(get), COMMAND_ARGS(config_get), nullptr, "Display configuration value."},
    {COMMAND(set), COMMAND_ARGS(config_set), nullptr},
    "Modify configuraiton value.",
    {}};

// Arguments for flash
static const cliSupport::Argument COMMAND_ARGS(flash)[] = {
    {COMMAND(dump), nullptr, run_app_dump_flash, "Show contents of EEPROM Emulation Flash."},
    {COMMAND(save), nullptr, run_app_save, "Save configuration across reset."},
    {}};

// Top-Level Arguments
static const cliSupport::Argument argtable[] = {
    {COMMAND(config), COMMAND_ARGS(config), nullptr, "Change runtime configuration."},
    {COMMAND(help), nullptr, run_app_help, "Display this help message."},
    {COMMAND(flash), COMMAND_ARGS(flash), nullptr, "Operations on persistent storage."},
    {}};

static void microrl_print_cbk(const char* s);
static int microrl_execute_callback(int argc, const char* const* argv);

static void microrl_print_cbk(const char* s) {
  printf(s);
  fflush(stdout);
}

static int microrl_execute_callback(int argc, const char* const* argv) {
  int result = cliSupport::callHandler(argtable, argc, argv);
  if (result == cliSupport::kNoHandler) {
    printf("No handler for command \"");
    for (int i = 0; i < argc; ++i) {
      printf("%s ", argv[i]);
    }
    printf("\"\n");
    display_help(argc, argv);
  }
  return result;
}

static char** microrl_complete_callback(int argc, const char* const* argv) {
  // Static buffer for the completion data passed to microrl.
  static const char* completion_data[NUM_COMPLETIONS];
  cliSupport::fillCompletionData(completion_data, NUM_COMPLETIONS - 1, argtable, argc, argv);
  return const_cast<char**>(completion_data);
}

int run_app_help(int, const char* const*, int) {
  display_help(0, nullptr);
  return 0;
}

void printArguments(int argc, const char* const* argv) {
  for (int i = 0; i < argc; ++i) {
    printf(argv[i]);
    printf(" ");
  }
}

void display_help(int argc, const char* const* argv) {
  cliSupport::PrefixResult prefix = cliSupport::findLongestPrefix(argtable, argc, argv);

  const cliSupport::Argument* argumentIt = nullptr;
  int validArgc = 0;

  if (prefix.empty()) {
    // No prefix match at all. Use root
    argumentIt = argtable;
    validArgc = 0;
  } else {
    argumentIt = prefix.arg->options;
    validArgc = prefix.level + 1;
  }

  puts("Available commands:");

  while ((argumentIt->name != nullptr)) {
    printf("  ");
    printArguments(validArgc, argv);
    printf(argumentIt->name);
    if (argumentIt->options != nullptr) {
      printf(" ...");
    }
    if (argumentIt->help != nullptr) {
      printf(" - ");
      printf(argumentIt->help);
    }
    printf("\n");
    ++argumentIt;
  }
}

void begin(DataModel* dataModel) {
  dataModel_ = dataModel;

  microrl_init(&microrl, microrl_print_cbk);
  microrl_set_execute_callback(&microrl, microrl_execute_callback);
  microrl_set_complete_callback(&microrl, microrl_complete_callback);
}

bool checkNumArgs(int argc, int lower, int upper, const char* appName) {
  static constexpr const char* formatString = "%s: Too %s arguments (%i expected, %i given).\n";
  if (argc < lower) {
    printf(formatString, appName, "few", lower, argc);
    return false;
  } else if (argc > upper) {
    printf(formatString, appName, "many", lower, argc);
    return false;
  } else {
    return true;
  }
}

int run_app_set_turnout_protocol(int argc, const char* const* argv, int argcMatched) {
  static constexpr const char* text{": Set Turnout protocol to "};
  static constexpr const char* appName{"SetTurnoutProtocol"};

  if (!checkNumArgs(argc - argcMatched, 1, 1, appName)) {
    display_help(argc, argv);
    return -2;
  }

  int protocolArgumentIdx = argcMatched;
  const char* protocolArgument = argv[protocolArgumentIdx];

  if (strncasecmp(protocolArgument, MM2Name, strlen(MM2Name)) == 0) {
    dataModel_->accessoryRailProtocol = RR32Can::RailProtocol::MM2;
    printf("%s%s'%s'.\n", appName, text, protocolArgument);
  } else if (strncasecmp(protocolArgument, DCCName, strlen(DCCName)) == 0) {
    dataModel_->accessoryRailProtocol = RR32Can::RailProtocol::DCC;
    printf("%s%s'%s'.\n", appName, text, argv[argcMatched + 1]);
  } else if (strncasecmp(protocolArgument, SX1Name, strlen(SX1Name)) == 0) {
    dataModel_->accessoryRailProtocol = RR32Can::RailProtocol::SX1;
    printf("%s%s'%s'.\n", appName, text, protocolArgument);
  } else {
    printf("%s: Unknown rail protocol '%s'.\n", appName, protocolArgument);
    return -3;
  }

  return 0;
}

int run_app_get_turnout_protocol(int argc, const char* const*, int argcMatched) {
  static constexpr const char* appName{"GetTurnoutProtocol"};

  if (!checkNumArgs(argc - argcMatched, 0, 0, appName)) {
    return -2;
  }

  const char* turnoutProtocol = nullptr;

  switch (dataModel_->accessoryRailProtocol) {
    case RR32Can::RailProtocol::MM1:
    case RR32Can::RailProtocol::MM2:
    case RR32Can::RailProtocol::MFX:
    case RR32Can::RailProtocol::UNKNOWN:
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

  printf("%s: The current turnout protocol is %s.\n", appName, turnoutProtocol);

  return 0;
}

int run_app_save(int argc, const char* const*, int argcMatched) {
  static constexpr const char* appName{"SaveToFlash"};

  if (!checkNumArgs(argc - argcMatched, 0, 0, appName)) {
    return -2;
  }

  hal::SaveConfig(*dataModel_);
  printf("%s: Configuration saved to flash.\n", appName);

  return 0;
}

int run_ln_slot_server_active(int argc, const char* const* argv, int argcMatched) {
  static constexpr const char* appName{"LnSlotServerActive"};
  if (!checkNumArgs(argc - argcMatched, 0, 0, appName)) {
    display_help(argc, argv);
    return -2;
  }

  dataModel_->lnSlotServerState = tasks::RoutingTask::LocoNetSlotServer::SlotServerState::ACTIVE;
  return 0;
}

int run_ln_slot_server_passive(int argc, const char* const* argv, int argcMatched) {
  static constexpr const char* appName{"LnSlotServerPassive"};
  if (!checkNumArgs(argc - argcMatched, 0, 0, appName)) {
    display_help(argc, argv);
    return -2;
  }

  dataModel_->lnSlotServerState = tasks::RoutingTask::LocoNetSlotServer::SlotServerState::PASSIVE;
  return 0;
}

int run_ln_slot_server_disable(int argc, const char* const* argv, int argcMatched) {
  static constexpr const char* appName{"LnSlotServerDisable"};
  if (!checkNumArgs(argc - argcMatched, 0, 0, appName)) {
    display_help(argc, argv);
    return -2;
  }

  dataModel_->lnSlotServerState = tasks::RoutingTask::LocoNetSlotServer::SlotServerState::DISABLED;
  return 0;
}

int run_ln_slot_server_getstatus(int argc, const char* const* argv, int argcMatched) {
  static constexpr const char* appName{"LnSlotServerGetstatus"};
  if (!checkNumArgs(argc - argcMatched, 0, 0, appName)) {
    display_help(argc, argv);
    return -2;
  }

  static constexpr const char* formatString{"LocoNet Slot Server is %s.\n"};

  switch (dataModel_->lnSlotServerState) {
    case tasks::RoutingTask::LocoNetSlotServer::SlotServerState::DISABLED:
      printf(formatString, COMMAND(disabled));
      break;
    case tasks::RoutingTask::LocoNetSlotServer::SlotServerState::PASSIVE:
      printf(formatString, COMMAND(passive));
      break;
    case tasks::RoutingTask::LocoNetSlotServer::SlotServerState::ACTIVE:
      printf(formatString, COMMAND(active));
      break;
    default:
      printf("Unknown slot server state %#02x.\n",
             static_cast<std::underlying_type<decltype(dataModel_->lnSlotServerState)>::type>(
                 dataModel_->lnSlotServerState));
      break;
  }
  return 0;
}

}  // namespace ConsoleManager