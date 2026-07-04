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
class InlineCodeParser : public LineParser
{
public:
  void Parse(std::string& line) override {
    static std::regex re("`([^`]*)`");
    static std::string replacement = "<code>$1</code>";
    line = std::regex_replace(line, re, replacement);
  }
};
} // namespace maddy