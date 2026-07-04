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
class QuoteParser : public BlockParser
{
public:
  QuoteParser(std::function<void(std::string&)> parseLineCallback,
    std::function<std::shared_ptr<BlockParser>(const std::string& line)> getBlockParserForLineCallback)
    : BlockParser(parseLineCallback, getBlockParserForLineCallback), isStarted(false), isFinished(false) {}
  static bool IsStartingLine(const std::string& line) {
    static std::regex re("^\\>.*");
    return std::regex_match(line, re);
  }
  void AddLine(std::string& line) override {
    if (!this->isStarted) { this->result << "<blockquote>"; this->isStarted = true; }
    bool finish = false;
    if (line.empty()) finish = true;
    this->parseBlock(line);
    if (this->isInlineBlockAllowed() && !this->childParser)
      this->childParser = this->getBlockParserForLine(line);
    if (this->childParser) {
      this->childParser->AddLine(line);
      if (this->childParser->IsFinished()) {
        this->result << this->childParser->GetResult().str();
        this->childParser = nullptr;
      }
      return;
    }
    if (this->isLineParserAllowed()) this->parseLine(line);
    if (finish) { this->result << "</blockquote>"; this->isFinished = true; }
    this->result << line;
  }
  bool IsFinished() const override { return this->isFinished; }
protected:
  bool isInlineBlockAllowed() const override { return true; }
  bool isLineParserAllowed() const override { return true; }
  void parseBlock(std::string& line) override {
    static std::regex lineRegexWithSpace("^\\> ");
    line = std::regex_replace(line, lineRegexWithSpace, "");
    static std::regex lineRegexWithoutSpace("^\\>");
    line = std::regex_replace(line, lineRegexWithoutSpace, "");
    if (!line.empty()) line += " ";
  }
private:
  bool isStarted; bool isFinished;
};
} // namespace maddy