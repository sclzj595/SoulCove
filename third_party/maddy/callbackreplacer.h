/*
 * This project is licensed under the MIT license. For more information see the
 * LICENSE file.
 */
#pragma once
#include <string>
#include <regex>
#include <functional>
namespace maddy {
inline std::string regex_callback_replacer(std::regex &regex, const std::string &input,
                                           std::function<std::string(std::smatch &)> &callback)
{
  std::string result;
  auto tagsbegin = std::sregex_iterator(input.begin(), input.end(), regex);
  auto tagsend = std::sregex_iterator();
  auto matchbegin = 0;
  for(std::sregex_iterator i = tagsbegin; i != tagsend; ++i)
  {
    std::smatch match = *i;
    auto matchlength = match.length(0);
    auto matchpos = match.position();
    result += input.substr(matchbegin, matchpos - matchbegin);
    result += callback(match);
    matchbegin = matchpos + matchlength;
  }
  result += input.substr(matchbegin);
  return result;
}
}