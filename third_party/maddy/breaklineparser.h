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
class BreakLineParser : public LineParser
{
public:
  void Parse(std::string& line) override {
    static std::regex re(R"((\r\n|\r))");
    static std::string replacement = "<br>\n";
    line = std::regex_replace(line, re, replacement);
  }
};
} // namespace maddy