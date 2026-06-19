/*
 * This project is licensed under the MIT license. For more information see the
 * LICENSE file.
 */
#pragma once
// -----------------------------------------------------------------------------
#include <string>
#include <regex>
#include "maddy/lineparser.h"
namespace maddy {
class StrikeThroughParser : public LineParser
{
public:
  void Parse(std::string& line) override {
    static std::regex re("(?!.*`.*|.*<code>.*)\\~\\~(?!.*`.*|.*<\\/code>.*)([^\\~]*)\\~\\~(?!.*`.*|.*<\\/code>.*)");
    static std::string replacement = "<s>$1</s>";
    line = std::regex_replace(line, re, replacement);
  }
};
} // namespace maddy