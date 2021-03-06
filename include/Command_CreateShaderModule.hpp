#pragma once

#include "Command.hpp"

class Command_CreateShaderModule : public Command
{
public:
  virtual ~Command_CreateShaderModule()
  {
  }

  virtual std::string const name() const override;

  virtual uint64_t requiredArguments() const override;

  virtual bool execute(std::vector<std::string> args) const override;
};
