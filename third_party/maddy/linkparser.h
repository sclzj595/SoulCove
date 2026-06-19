/*
 * This project is licensed under the MIT license. For more information see the
 * LICENSE file.
 */
#pragma once
// -----------------------------------------------------------------------------
#include <string>
#include <regex>
#include "maddy/lineparser.h"
#include "maddy/callbackreplacer.h"
namespace maddy {
// 增强: 同时支持 [text](url) 标准链接 和 裸 URL 自动链接 (https?://...)
class LinkParser : public LineParser
{
private:
  // 单正则 alternation: 优先匹配 [text](url)，其次匹配裸 URL
  std::regex re = std::regex("(\\[[^\\]]*\\]\\([^\\)]*\\))|(https?://[^\\s<>\"\\)]+)");
  std::function<std::string(std::smatch &)> callback = [](std::smatch & match){
    // 分支1: [text](url)
    if (match[1].matched)
    {
      static std::regex linkRe("\\[([^\\]]*)\\]\\(([^\\)]*)\\)");
      std::smatch lm;
      std::string s = match[1].str();
      if (std::regex_match(s, lm, linkRe))
        return "<a href=\"" + lm[2].str() + "\">" + lm[1].str() + "</a>";
      return s;
    }
    // 分支2: 裸 URL 自动链接
    std::string url = match[2].str();
    return "<a href=\"" + url + "\">" + url + "</a>";
  };
public:
  void Parse(std::string& line) override {
    line = regex_callback_replacer(re,line,callback);
  }
};
} // namespace maddy
