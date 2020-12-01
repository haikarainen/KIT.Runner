#pragma once

#include <string>
#include <vector>
#include <cstdint>

class Command
{
public:
  virtual ~Command() {}

  virtual std::string const name() const = 0;
  virtual std::string const imports() const  { return ""; }
  virtual bool execute(std::vector<std::string> args) const = 0;
  virtual uint64_t requiredArguments() const = 0;
protected:

};