/*
 * This project is licensed under the MIT license. For more information see the
 * LICENSE file.
 */
#pragma once
// -----------------------------------------------------------------------------
#include <functional>
#include <regex>
#include <string>
#include "maddy/blockparser.h"
namespace maddy {
class UnorderedListParser : public BlockParser
{
public:
  UnorderedListParser(std::function<void(std::string&)> parseLineCallback,
    std::function<std::shared_ptr<BlockParser>(const std::string& line)> getBlockParserForLineCallback)
    : BlockParser(parseLineCallback, getBlockParserForLineCallback), isStarted(false), isFinished(false) {}
  static bool IsStartingLine(const std::string& line) {
    static std::regex re("^[+*-] .*");
    return std::regex_match(line, re);
  }
  bool IsFinished() const override { return this->isFinished; }
protected:
  bool isInlineBlockAllowed() const override { return true; }
  bool isLineParserAllowed() const override { return true; }
  void parseBlock(std::string& line) override {
    bool isStartOfNewListItem = IsStartingLine(line);
    uint32_t indentation = getIndentationWidth(line);
    static std::regex lineRegex("^([+*-] )");
    line = std::regex_replace(line, lineRegex, "");
    if (!this->isStarted) { line = "<ul><li>" + line; this->isStarted = true; return; }
    if (indentation >= 2) { line = line.substr(2); return; }
    if (line.empty() || line.find("</li><li>") != std::string::npos ||
        line.find("</li></ol>") != std::string::npos || line.find("</li></ul>") != std::string::npos) {
      line = "</li></ul>" + line; this->isFinished = true; return;
    }
    if (isStartOfNewListItem) line = "</li><li>" + line;
  }
private:
  bool isStarted, isFinished;
};
} // namespace maddy