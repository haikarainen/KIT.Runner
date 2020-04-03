#pragma once

#include "Command.hpp"

class Command_TestCompression : public Command
{
public:
  virtual ~Command_TestCompression();

  virtual std::string const name() const override;
  virtual bool execute(std::vector<std::string> args) const override;;

  virtual uint64_t requiredArguments() const override;;
protected:

};