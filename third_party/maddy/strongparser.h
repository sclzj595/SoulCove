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
class StrongParser : public LineParser
{
public:
  void Parse(std::string& line) override {
    static std::vector<std::regex> res {
      std::regex{"(?!.*`.*|.*<code>.*)\\*\\*(?!.*`.*|.*<\\/code>.*)([^\\*\\*]*)\\*\\*(?!.*`.*|.*<\\/code>.*)"},
      std::regex{"(?!.*`.*|.*<code>.*)__(?!.*`.*|.*<\\/code>.*)([^__]*)__(?!.*`.*|.*<\\/code>.*)"}
    };
    static std::string replacement = "<strong>$1</strong>";
    for (const auto& re : res)
      line = std::regex_replace(line, re, replacement);
  }
};
} // namespace maddy