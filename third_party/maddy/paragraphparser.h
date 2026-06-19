/*
 * This project is licensed under the MIT license. For more information see the
 * LICENSE file.
 */
#pragma once
// -----------------------------------------------------------------------------
#include <functional>
#include <string>
#include "maddy/blockparser.h"
namespace maddy {
class ParagraphParser : public BlockParser
{
public:
  ParagraphParser(std::function<void(std::string&)> parseLineCallback,
    std::function<std::shared_ptr<BlockParser>(const std::string& line)> getBlockParserForLineCallback)
    : BlockParser(parseLineCallback, getBlockParserForLineCallback), isStarted(false), isFinished(false) {}
  static bool IsStartingLine(const std::string& line) { return !line.empty(); }
  bool IsFinished() const override { return this->isFinished; }
protected:
  bool isInlineBlockAllowed() const override { return false; }
  bool isLineParserAllowed() const override { return true; }
  void parseBlock(std::string& line) override {
    if (!this->isStarted) { line = "<p>" + line + " "; this->isStarted = true; return; }
    if (line.empty()) { line += "</p>"; this->isFinished = true; return; }
    line += " ";
  }
private:
  bool isStarted; bool isFinished;
};
} // namespace maddy