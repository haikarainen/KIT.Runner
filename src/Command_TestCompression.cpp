#include "Command_TestCompression.hpp"

#include <WIR/Stream.hpp>

Command_TestCompression::~Command_TestCompression()
{

}

std::string const Command_TestCompression::name() const
{
  return "test_compression";
}

bool Command_TestCompression::execute(std::vector<std::string> args) const
{
  std::vector<uint8_t> testData = { 0x00, 0x11, 0x44, 0x22, 0x33, 'f', 'o', 'o', 'b', 'a', 'r' };

  wir::Stream testStream;
  testStream.write(testData.data(), testData.size());

  wir::Stream compressedStream;
  testStream.compress(compressedStream);

  wir::Stream decompressedStream;
  compressedStream.decompress(decompressedStream, testStream.size());

  std::vector<uint8_t> readData;
  decompressedStream.read(readData);

  return readData == testData;
}

uint64_t Command_TestCompression::requiredArguments() const
{
  return 2;
}
