#pragma once

#include "Command.hpp"

class Command_ImportMesh : public Command
{
public:
  virtual ~Command_ImportMesh();

  virtual std::string const name() const override;
  virtual bool execute(std::vector<std::string> args) const override;
  ;

  virtual uint64_t requiredArguments() const override;
  ;

  virtual std::string const imports() const override
  {
    return "Mesh";
  }

protected:
};