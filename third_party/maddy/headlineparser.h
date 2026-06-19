/*
 * This project is licensed under the MIT license. For more information see the
 * LICENSE file.
 */
#pragma once
// -----------------------------------------------------------------------------
#include <functional>
#include <string>
#include <regex>
#include <cctype>
#include "maddy/blockparser.h"
// -----------------------------------------------------------------------------
namespace maddy {
// 增强: 标题输出带 id 属性 (anchor)，用于 TOC 面板点击跳转
// anchor 生成规则与 MarkdownMode::parseToc 保持一致
class HeadlineParser : public BlockParser
{
public:
  HeadlineParser(std::function<void(std::string&)> parseLineCallback,
    std::function<std::shared_ptr<BlockParser>(const std::string& line)> getBlockParserForLineCallback)
    : BlockParser(parseLineCallback, getBlockParserForLineCallback) {}
  static bool IsStartingLine(const std::string& line)
  {
    static std::regex re("^(?:#){1,6} (.*)");
    return std::regex_match(line, re);
  }
  bool IsFinished() const override { return true; }
protected:
  bool isInlineBlockAllowed() const override { return false; }
  bool isLineParserAllowed() const override { return false; }
  void parseBlock(std::string& line) override
  {
    static std::vector<std::regex> hlRegex = {
      std::regex("^# (.*)"), std::regex("^(?:#){2} (.*)"),
      std::regex("^(?:#){3} (.*)"), std::regex("^(?:#){4} (.*)"),
      std::regex("^(?:#){5} (.*)"), std::regex("^(?:#){6} (.*)")
    };
    for (uint8_t i = 0; i < 6; ++i)
    {
      std::smatch m;
      if (std::regex_match(line, m, hlRegex[i]))
      {
        std::string text = m[1].str();
        std::string anchor = toAnchorId(text);
        std::string tag = "h" + std::to_string(i + 1);
        if (anchor.empty())
          line = "<" + tag + ">" + text + "</" + tag + ">";
        else
          line = "<" + tag + " id=\"" + anchor + "\">" + text + "</" + tag + ">";
        break;
      }
    }
  }
private:
  // 生成 anchor id，与 MarkdownMode::parseToc 的 Qt 实现保持一致:
  //   toLower -> 移除非[a-z0-9中文-_] -> 连续__/--合并为- -> 去首尾-_
  static std::string toAnchorId(const std::string& text)
  {
    std::string s = text;
    for (auto& c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // 保留 [a-z0-9-_] 及多字节字符 (>=0x80, 含中文)，移除其余
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
      unsigned char c = static_cast<unsigned char>(s[i]);
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
        out += static_cast<char>(c);
      else if (c >= 0x80)
        out += s[i]; // 保留多字节字符
    }
    s = out;

    // 连续 2+ 个相同 - 或 _ 合并为单个 -
    std::string s2;
    for (size_t i = 0; i < s.size(); ++i)
    {
      if ((s[i] == '-' || s[i] == '_') && i + 1 < s.size() && s[i + 1] == s[i])
      {
        s2 += '-';
        size_t j = i + 1;
        while (j < s.size() && s[j] == s[i]) ++j;
        i = j - 1;
      }
      else
      {
        s2 += s[i];
      }
    }
    s = s2;

    // 去除首尾 - 或 _
    while (!s.empty() && (s.front() == '-' || s.front() == '_')) s.erase(s.begin());
    while (!s.empty() && (s.back() == '-' || s.back() == '_')) s.pop_back();
    return s;
  }
};
} // namespace maddy
