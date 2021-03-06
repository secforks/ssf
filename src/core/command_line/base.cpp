#include <cstdint>

#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <regex>
#include <memory>

#include <boost/program_options.hpp>
#include <boost/system/error_code.hpp>

#include "common/error/error.h"

#include "core/command_line/base.h"

#include <ssf/log/log.h>

#include "versions.h"

namespace ssf {
namespace command_line {

BaseCommandLine::BaseCommandLine()
    : host_(""),
      port_(),
      config_file_(""),
      circuit_file_(""),
      log_level_(ssf::log::kLogInfo),
      host_set_(false),
      port_set_(false) {}

BaseCommandLine::ParsedParameters BaseCommandLine::Parse(
    int argc, char* argv[], boost::system::error_code& ec) {
  OptionDescription services;
  return Parse(argc, argv, services, ec);
}

BaseCommandLine::ParsedParameters BaseCommandLine::Parse(
    int ac, char* av[], const OptionDescription& services,
    boost::system::error_code& ec) {
  // Basic options
  OptionDescription basic_opts("Basic options");
  InitBasicOptions(basic_opts);
  PopulateBasicOptions(basic_opts);

  // Local options
  OptionDescription local_opts("Local options");
  InitLocalOptions(local_opts);
  PopulateLocalOptions(local_opts);

  try {
    OptionDescription cmd_line;

    cmd_line.add(basic_opts).add(local_opts);

    // Other options
    PopulateCommandLine(cmd_line);

    // Positional options
    PosOptionDescription pos_opts;
    PopulatePositionalOptions(pos_opts);

    cmd_line.add(services);

    VariableMap vm;
    boost::program_options::store(
        boost::program_options::command_line_parser(ac, av)
            .options(cmd_line)
            .positional(pos_opts)
            .run(),
        vm);

    if (DisplayHelp(vm, cmd_line)) {
      ec.assign(::error::operation_canceled, ::error::get_ssf_category());
      return {};
    }

    boost::program_options::notify(vm);

    return DoParse(services, vm, ec);
  } catch (const std::exception& e) {
    SSF_LOG(kLogCritical) << "command line: parsing failed: " << e.what();
    ec.assign(::error::invalid_argument, ::error::get_ssf_category());
    return std::map<std::string, std::vector<std::string>>();
  }
}

void BaseCommandLine::PopulateBasicOptions(OptionDescription& basic_opts) {}

void BaseCommandLine::PopulateLocalOptions(OptionDescription& local_opts) {}

void BaseCommandLine::PopulatePositionalOptions(
    PosOptionDescription& pos_opts) {}

void BaseCommandLine::PopulateCommandLine(OptionDescription& command_line) {}

void BaseCommandLine::ParseOptions(const VariableMap& value,
                                   ParsedParameters& parsed_params,
                                   boost::system::error_code& ec) {}

bool BaseCommandLine::IsServerCli() { return false; }

bool BaseCommandLine::DisplayHelp(const VariableMap& vm,
                                  const OptionDescription& cli) {
  if (!vm.count("help")) {
    return false;
  }

  std::cout << "SSF " << ssf::versions::major << "." << ssf::versions::minor
            << "." << ssf::versions::fix << std::endl << std::endl;

  std::cout << "Usage: " << GetUsageDesc() << std::endl;

  std::cout << cli << std::endl;

  std::cout << "Using Boost " << ssf::versions::boost_version << " and OpenSSL "
            << ssf::versions::openssl_version << std::endl
            << std::endl;

  return true;
}

void BaseCommandLine::ParseBasicOptions(const VariableMap& vm,
                                        boost::system::error_code& ec) {
  for (const auto& option : vm) {
    if (option.first == "quiet") {
      log_level_ = ssf::log::kLogNone;
    } else if (option.first == "verbosity") {
      set_log_level(option.second.as<std::string>());
    } else if (option.first == "port") {
      int port = option.second.as<int>();
      if (port > 0 && port < 65536) {
        port_ = static_cast<uint16_t>(port);
        port_set_ = true;
      } else {
        SSF_LOG(kLogError)
            << "command line: parsing failed: port option is not "
               "between 1 - 65536";
        ec.assign(::error::invalid_argument, ::error::get_ssf_category());
      }
    } else if (option.first == "circuit") {
      circuit_file_ = option.second.as<std::string>();
    } else if (option.first == "config") {
      config_file_ = option.second.as<std::string>();
    }
  }
}

BaseCommandLine::ParsedParameters BaseCommandLine::DoParse(
    const OptionDescription& services, const VariableMap& vm,
    boost::system::error_code& ec) {
  ParsedParameters result;

  ParseBasicOptions(vm, ec);
  if (ec) {
    return {};
  }

  ParseOptions(vm, result, ec);
  if (ec) {
    return {};
  }

  for (const auto& option : vm) {
    if (services.find_nothrow(option.first, false) != nullptr) {
      // Register service options
      result[option.first] = option.second.as<std::vector<std::string>>();
    }
  }

  ec.assign(::error::success, ::error::get_ssf_category());

  return result;
}

void BaseCommandLine::InitBasicOptions(OptionDescription& basic_opts) {
  // clang-format off
  basic_opts.add_options()
    ("help,h", "Produce help message");

  basic_opts.add_options()
    ("verbosity,v",
        boost::program_options::value<std::string>()
          ->value_name("level")
          ->default_value("info"),
        "Verbosity:\n  critical|error|warning|info|debug|trace");

  basic_opts.add_options()
    ("quiet,q", "Do not display log");
  // clang-format on
}

void BaseCommandLine::InitLocalOptions(OptionDescription& local_opts) {
  // clang-format off
  local_opts.add_options()
    ("config,c",
        boost::program_options::value<std::string>()
          ->value_name("config_file_path"),
        "Set config file");

  if (!IsServerCli()) {
    local_opts.add_options()
      ("circuit,b",
          boost::program_options::value<std::string>()
            ->value_name("circuit_file_path"),
          "Set circuit file");
    
    local_opts.add_options()
      ("port,p",
          boost::program_options::value<int>()->default_value(8011)
            ->value_name("port"),
          "Set remote SSF server port");
  } else {
    local_opts.add_options()
      ("port,p",
          boost::program_options::value<int>()->default_value(8011)
            ->value_name("port"),
          "Set local SSF server port");
  }
  // clang-format on
}

void BaseCommandLine::set_log_level(const std::string& level) {
  if (log_level_ == ssf::log::kLogNone) {
    // Quiet set
    return;
  }

  if (level == "critical") {
    log_level_ = ssf::log::kLogCritical;
  } else if (level == "error") {
    log_level_ = ssf::log::kLogError;
  } else if (level == "warning") {
    log_level_ = ssf::log::kLogWarning;
  } else if (level == "info") {
    log_level_ = ssf::log::kLogInfo;
  } else if (level == "debug") {
    log_level_ = ssf::log::kLogDebug;
  } else if (level == "trace") {
    log_level_ = ssf::log::kLogTrace;
  } else {
    log_level_ = ssf::log::kLogInfo;
  }
}

}  // command_line
}  // ssf