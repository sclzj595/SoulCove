/*
 * This project is licensed under the MIT license. For more information see the
 * LICENSE file.
 */
#pragma once
// -----------------------------------------------------------------------------
#include <functional>
#include <string>
#include <regex>
#include "maddy/blockparser.h"
// -----------------------------------------------------------------------------
namespace maddy {
// -----------------------------------------------------------------------------
// 增强: 支持 ```lang 语言标识 (GFM info string)，输出 <pre><code class="language-xxx">
//       并对代码内容做 HTML 实体转义，防止破坏 HTML 结构
class CodeBlockParser : public BlockParser
{
public:
   CodeBlockParser(
    std::function<void(std::string&)> parseLineCallback,
    std::function<std::shared_ptr<BlockParser>(const std::string& line)> getBlockParserForLineCallback
  )
    : BlockParser(parseLineCallback, getBlockParserForLineCallback)
    , isStarted(false)
    , isFinished(false)
  {}
  static bool
  IsStartingLine(const std::string& line)
  {
    // 匹配 ``` 及 ```cpp / ``` python 等 (行首三个反引号开头)
    return line.size() >= 3 && line.compare(0, 3, "```") == 0;
  }
  bool
  IsFinished() const override
  {
    return this->isFinished;
  }
protected:
  bool
  isInlineBlockAllowed() const override
  {
    return false;
  }
  bool
  isLineParserAllowed() const override
  {
    return false;
  }
  void
  parseBlock(std::string& line) override
  {
    // 结束 fence: 纯 ```
    if (line == "```")
    {
      if (!this->isStarted)
      {
        line = "<pre><code>\n";
        this->isStarted = true;
        this->isFinished = false;
        return;
      }
      else
      {
        line = "</code></pre>";
        this->isFinished = true;
        this->isStarted = false;
        return;
      }
    }
    // 第一行: ```lang  (提取 info string 作为语言标识)
    if (!this->isStarted)
    {
      this->lang = extractLang(line);
      if (this->lang.empty())
      {
        line = "<pre><code>\n";
      }
      else
      {
        line = "<pre><code class=\"language-" + this->lang + "\">\n";
      }
      this->isStarted = true;
      return;
    }
    // 代码内容行: 转义 HTML 实体后追加换行
    line = escapeXml(line);
    line += "\n";
  }
private:
  bool isStarted;
  bool isFinished;
  std::string lang;

  static std::string extractLang(const std::string& line)
  {
    if (line.size() <= 3) return std::string();
    std::string info = line.substr(3);
    // 去除首尾空白
    size_t b = info.find_first_not_of(" \t");
    size_t e = info.find_last_not_of(" \t");
    if (b == std::string::npos) return std::string();
    info = info.substr(b, e - b + 1);
    // 取第一个空白分隔的 token 作为语言标识 (忽略 info string 附加属性)
    size_t sp = info.find_first_of(" \t");
    if (sp != std::string::npos) info = info.substr(0, sp);
    return info;
  }

  static std::string escapeXml(const std::string& s)
  {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
      switch (c)
      {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c; break;
      }
    }
    return out;
  }
}; // class CodeBlockParser
// -----------------------------------------------------------------------------
} // namespace maddy
