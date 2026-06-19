/*
 * This project is licensed under the MIT license. For more information see the
 * LICENSE file.
 */
#pragma once
// -----------------------------------------------------------------------------
#include <functional>
#include <string>
#include <regex>
#include "maddy/blockparser.h"
namespace maddy {
class HorizontalLineParser : public BlockParser
{
public:
  HorizontalLineParser(std::function<void(std::string&)> parseLineCallback,
    std::function<std::shared_ptr<BlockParser>(const std::string& line)> getBlockParserForLineCallback)
    : BlockParser(parseLineCallback, getBlockParserForLineCallback), lineRegex("^---$") {}
  static bool IsStartingLine(const std::string& line) {
    static std::regex re("^---$");
    return std::regex_match(line, re);
  }
  bool IsFinished() const override { return true; }
protected:
  bool isInlineBlockAllowed() const override { return false; }
  bool isLineParserAllowed() const override { return false; }
  void parseBlock(std::string& line) override {
    static std::string replacement = "<hr/>";
    line = std::regex_replace(line, lineRegex, replacement);
  }
private:
  std::regex lineRegex;
};
} // namespace maddy