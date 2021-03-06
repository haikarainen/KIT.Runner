#pragma once

#include "Command.hpp"

class Command_ImportTexture : public Command
{
public:
  virtual ~Command_ImportTexture();

  virtual std::string const name() const override;
  virtual bool execute(std::vector<std::string> args) const override;

  virtual std::string const imports() const override
  {
    return "Texture";
  }

  virtual uint64_t requiredArguments() const override;
  ;

protected:
};