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
class EmphasizedParser : public LineParser
{
public:
  void Parse(std::string& line) override {
    static std::regex re("(?!.*`.*|.*<code>.*)_(?!.*`.*|.*<\\/code>.*)([^_]*)_(?!.*`.*|.*<\\/code>.*)");
    static std::string replacement = "<em>$1</em>";
    line = std::regex_replace(line, re, replacement);
  }
};
} // namespace maddy