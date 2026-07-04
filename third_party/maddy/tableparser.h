/*
 * This project is licensed under the MIT license. For more information see the
 * LICENSE file.
 */
#pragma once
// -----------------------------------------------------------------------------
#include <functional>
#include <string>
#include <regex>
#include <vector>
#include "maddy/blockparser.h"
namespace maddy {
// 增强: 支持标准 GFM 表格语法
//   | H1 | H2 |
//   |----|----|
//   | a  | b  |
// 兼容对齐标记 :--- / :---: / ---:
class TableParser : public BlockParser
{
public:
  TableParser(std::function<void(std::string&)> parseLineCallback,
    std::function<std::shared_ptr<BlockParser>(const std::string& line)> getBlockParserForLineCallback)
    : BlockParser(parseLineCallback, getBlockParserForLineCallback), isStarted(false), isFinished(false) {}

  static bool IsStartingLine(const std::string& line)
  {
    // GFM 表格行: 以 | 开头 (允许前导空白)
    static std::regex re("^\\s*\\|");
    return std::regex_search(line, re);
  }

  void AddLine(std::string& line) override
  {
    if (!this->isStarted) this->isStarted = true;

    // 空行 -> 结束表格
    if (line.empty())
    {
      this->finish(false);
      return;
    }

    // 非表格行 -> 结束表格，并把该行原样保留 (避免内容丢失)
    static std::regex tableLineRe("^\\s*\\|");
    if (!std::regex_search(line, tableLineRe))
    {
      this->finish(false);
      // 该行作为裸文本追加 (供后续显示，虽不会被段落包裹)
      this->result << line << "\n";
      return;
    }

    this->rows.push_back(line);
  }

  bool IsFinished() const override { return this->isFinished; }

protected:
  bool isInlineBlockAllowed() const override { return false; }
  bool isLineParserAllowed() const override { return true; }
  void parseBlock(std::string&) override {} // AddLine 全权处理

private:
  bool isStarted, isFinished;
  std::vector<std::string> rows;

  // 解析一行表格为 cell 列表 (按 | 分割，去首尾空段，trim)
  static std::vector<std::string> parseRow(const std::string& line)
  {
    std::vector<std::string> cells;
    std::string seg;
    std::stringstream ss(line);
    while (std::getline(ss, seg, '|'))
      cells.push_back(seg);
    // 去掉首尾空段 (因行首/尾的 | 产生空串)
    if (!cells.empty() && cells.front().empty()) cells.erase(cells.begin());
    if (!cells.empty() && cells.back().empty()) cells.pop_back();
    // trim 每个 cell
    for (auto& c : cells) c = trim(c);
    return cells;
  }

  static std::string trim(const std::string& s)
  {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
  }

  // 判断 cell 是否为对齐分隔符 (:--- / --- / :---: / ---:)
  static bool isSeparator(const std::string& c)
  {
    if (c.empty()) return false;
    static std::regex re("^:?-{3,}:?$");
    return std::regex_match(c, re);
  }

  static std::string alignOf(const std::string& c)
  {
    bool left = !c.empty() && c.front() == ':';
    bool right = !c.empty() && c.back() == ':';
    if (left && right) return "center";
    if (right) return "right";
    return "left"; // default / :--- / ---
  }

  void finish(bool /*trailing*/)
  {
    this->isFinished = true;

    // 行数不足或第二行不是分隔符 -> 不是合法 GFM 表格，按普通文本输出
    if (rows.size() < 2)
    {
      for (const auto& r : rows)
      {
        std::string s = r;
        if (isLineParserAllowed()) this->parseLine(s);
        this->result << s << "<br>";
      }
      return;
    }

    std::vector<std::string> headerCells = parseRow(rows[0]);
    std::vector<std::string> sepCells = parseRow(rows[1]);

    // 校验第二行是否全为分隔符
    bool validSep = !sepCells.empty();
    for (const auto& c : sepCells) { if (!isSeparator(c)) { validSep = false; break; } }
    if (!validSep)
    {
      for (const auto& r : rows)
      {
        std::string s = r;
        if (isLineParserAllowed()) this->parseLine(s);
        this->result << s << "<br>";
      }
      return;
    }

    // 计算每列对齐
    std::vector<std::string> aligns;
    for (size_t i = 0; i < headerCells.size(); ++i)
      aligns.push_back(i < sepCells.size() ? alignOf(sepCells[i]) : std::string("left"));

    this->result << "<table><thead><tr>";
    for (size_t i = 0; i < headerCells.size(); ++i)
    {
      std::string c = headerCells[i];
      if (isLineParserAllowed()) this->parseLine(c);
      this->result << "<th style=\"text-align:" << aligns[i] << "\">" << c << "</th>";
    }
    this->result << "</tr></thead><tbody>";

    for (size_t r = 2; r < rows.size(); ++r)
    {
      std::vector<std::string> bodyCells = parseRow(rows[r]);
      this->result << "<tr>";
      for (size_t i = 0; i < headerCells.size(); ++i)
      {
        std::string c = (i < bodyCells.size()) ? bodyCells[i] : std::string();
        if (isLineParserAllowed()) this->parseLine(c);
        this->result << "<td style=\"text-align:" << aligns[i] << "\">" << c << "</td>";
      }
      this->result << "</tr>";
    }
    this->result << "</tbody></table>";
  }
};
} // namespace maddy
