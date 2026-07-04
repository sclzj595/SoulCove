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
class OrderedListParser : public BlockParser
{
public:
  OrderedListParser(std::function<void(std::string&)> parseLineCallback,
    std::function<std::shared_ptr<BlockParser>(const std::string& line)> getBlockParserForLineCallback)
    : BlockParser(parseLineCallback, getBlockParserForLineCallback), isStarted(false), isFinished(false) {}
  static bool IsStartingLine(const std::string& line) {
    static std::regex re("^1\\. .*");
    return std::regex_match(line, re);
  }
  bool IsFinished() const override { return this->isFinished; }
protected:
  bool isInlineBlockAllowed() const override { return true; }
  bool isLineParserAllowed() const override { return true; }
  void parseBlock(std::string& line) override {
    bool isStartOfNewListItem = this->isStartOfNewListItem(line);
    uint32_t indentation = getIndentationWidth(line);
    static std::regex orderedlineRegex("^[1-9]+[0-9]*\\. ");
    line = std::regex_replace(line, orderedlineRegex, "");
    static std::regex unorderedlineRegex("^\\* ");
    line = std::regex_replace(line, unorderedlineRegex, "");
    if (!this->isStarted) { line = "<ol><li>" + line; this->isStarted = true; return; }
    if (indentation >= 2) { line = line.substr(2); return; }
    if (line.empty() || line.find("</li><li>") != std::string::npos ||
        line.find("</li></ol>") != std::string::npos || line.find("</li></ul>") != std::string::npos) {
      line = "</li></ol>" + line; this->isFinished = true; return;
    }
    if (isStartOfNewListItem) line = "</li><li>" + line;
  }
private:
  bool isStarted, isFinished;
  bool isStartOfNewListItem(const std::string& line) const {
    static std::regex re("^(?:[1-9]+[0-9]*\\. |\\* ).*");
    return std::regex_match(line, re);
  }
};
} // namespace maddy