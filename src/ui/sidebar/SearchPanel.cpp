#include "ui/sidebar/SearchPanel.h"
#include "Logger.hpp"

#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QCoreApplication>

/// 符号扫描结果条目（文件内部使用，不暴露到头文件）
struct SymbolEntry {
    QString name;
    int line;
    QString icon;
};

SearchPanel::SearchPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 4, 4);
    layout->setSpacing(4);

    auto* title = new QLabel(tr("搜索"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(title);

    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(tr("搜索内容..."));
    m_searchInput->setObjectName(QStringLiteral("searchInput"));
    layout->addWidget(m_searchInput);

    // 替换输入框（V1.9）
    m_replaceInput = new QLineEdit(this);
    m_replaceInput->setPlaceholderText(tr("替换为..."));
    m_replaceInput->setObjectName(QStringLiteral("replaceInput"));
    layout->addWidget(m_replaceInput);

    // 选项行：大小写/正则 + 全部替换按钮
    auto* optLayout = new QHBoxLayout();
    optLayout->setSpacing(4);
    m_chkCaseSensitive = new QCheckBox(tr("Aa"), this);
    m_chkCaseSensitive->setToolTip(tr("区分大小写"));
    m_chkRegex = new QCheckBox(tr(".*"), this);
    m_chkRegex->setToolTip(tr("正则表达式"));
    m_chkSymbolSearch = new QCheckBox(tr("§"), this);  // V1.9: 符号搜索
    m_chkSymbolSearch->setToolTip(tr("符号搜索模式（全局搜索 class/function/struct 等符号定义）"));
    m_btnReplaceAll = new QPushButton(tr("全部替换"), this);
    m_btnReplaceAll->setToolTip(tr("在所有文件中替换"));
    optLayout->addWidget(m_chkCaseSensitive);
    optLayout->addWidget(m_chkRegex);
    optLayout->addWidget(m_chkSymbolSearch);
    optLayout->addStretch();
    optLayout->addWidget(m_btnReplaceAll);
    layout->addLayout(optLayout);

    // 文件类型过滤（V1.9）
    m_fileFilterInput = new QLineEdit(this);
    m_fileFilterInput->setPlaceholderText(tr("文件类型: *.cpp,*.h"));
    m_fileFilterInput->setObjectName(QStringLiteral("fileFilterInput"));
    layout->addWidget(m_fileFilterInput);

    m_searchResults = new QListWidget(this);
    m_searchResults->setObjectName(QStringLiteral("sideFileList"));
    layout->addWidget(m_searchResults);

    // === 信号连接 ===
    connect(m_searchInput, &QLineEdit::returnPressed,
            this, &SearchPanel::onSearchTriggered);
    connect(m_searchResults, &QListWidget::itemDoubleClicked,
            this, &SearchPanel::onResultDoubleClicked);
    connect(m_btnReplaceAll, &QPushButton::clicked,
            this, &SearchPanel::onReplaceAll);
}

void SearchPanel::setWorkspaceFolders(const QStringList& folders)
{
    m_workspaceFolders = folders;
}

// ============================================================
// 搜索逻辑
// ============================================================

void SearchPanel::onSearchTriggered()
{
    QString keyword = m_searchInput->text().trimmed();
    m_searchResults->clear();

    if (keyword.isEmpty() && !(m_chkSymbolSearch && m_chkSymbolSearch->isChecked())) return;

    // V1.9: 符号搜索模式
    if (m_chkSymbolSearch && m_chkSymbolSearch->isChecked()) {
        performSymbolSearch(keyword);
        return;
    }

    // V1.9: 遍历工作区所有文件夹
    if (m_workspaceFolders.isEmpty()) {
        // 无工作区时使用默认 Files 目录
        QString filesDir = QCoreApplication::applicationDirPath() + QStringLiteral("/Files");
        QDir dir(filesDir);
        if (!dir.exists()) dir.mkpath(filesDir);
        searchInDirectory(dir, keyword);
    } else {
        for (const QString& folder : m_workspaceFolders) {
            QDir dir(folder);
            if (dir.exists()) {
                searchInDirectory(dir, keyword);
            }
        }
    }

    if (m_searchResults->count() == 0) {
        m_searchResults->addItem(tr("未找到匹配结果"));
    }
}

void SearchPanel::searchInDirectory(const QDir& dir, const QString& keyword)
{
    // 递归子目录
    QFileInfoList dirEntries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : dirEntries) {
        searchInDirectory(QDir(fi.absoluteFilePath()), keyword);
    }

    // 准备正则表达式（如果启用）
    QRegularExpression regex;
    if (m_chkRegex && m_chkRegex->isChecked()) {
        regex.setPattern(keyword);
        if (!m_chkCaseSensitive || !m_chkCaseSensitive->isChecked()) {
            regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        }
        if (!regex.isValid()) return;
    }

    Qt::CaseSensitivity cs = (m_chkCaseSensitive && m_chkCaseSensitive->isChecked()) ?
        Qt::CaseSensitive : Qt::CaseInsensitive;

    // 搜索文件内容
    QFileInfoList fileEntries = dir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : fileEntries) {
        // 跳过二进制文件
        QString suffix = fi.suffix().toLower();
        if (suffix == QStringLiteral("exe") || suffix == QStringLiteral("dll") ||
            suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") ||
            suffix == QStringLiteral("ico") || suffix == QStringLiteral("zip"))
            continue;

        // V1.9: 文件类型过滤
        if (!matchesFileFilter(fi.fileName())) continue;

        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QTextStream in(&file);
        int lineNum = 0;
        while (!in.atEnd()) {
            ++lineNum;
            QString line = in.readLine();
            bool matched = false;
            if (m_chkRegex && m_chkRegex->isChecked()) {
                matched = regex.match(line).hasMatch();
            } else {
                matched = line.contains(keyword, cs);
            }
            if (matched) {
                QString display = QStringLiteral("%1:%2  %3")
                    .arg(fi.fileName())
                    .arg(lineNum)
                    .arg(line.trimmed().left(60));
                auto* item = new QListWidgetItem(display, m_searchResults);
                item->setData(Qt::UserRole, fi.absoluteFilePath());
                item->setData(Qt::UserRole + 1, lineNum - 1);  // 转为 0-based
                item->setData(Qt::UserRole + 2, QStringLiteral("text"));  // 标记为文本搜索
                item->setToolTip(fi.absoluteFilePath() + QStringLiteral(":") + QString::number(lineNum));
            }
        }
        file.close();
    }
}

bool SearchPanel::matchesFileFilter(const QString& fileName) const
{
    if (!m_fileFilterInput || m_fileFilterInput->text().trimmed().isEmpty())
        return true;  // 无过滤条件，匹配所有

    QString filterText = m_fileFilterInput->text().trimmed();
    // 支持逗号分隔的多个模式：*.cpp,*.h,*.py
    QStringList patterns = filterText.split(QStringLiteral(","),
        Qt::SkipEmptyParts);

    for (const QString& pattern : patterns) {
        QString p = pattern.trimmed();
        QRegularExpression re(
            QRegularExpression::wildcardToRegularExpression(p),
            QRegularExpression::CaseInsensitiveOption);
        if (re.match(fileName).hasMatch()) return true;
    }
    return false;
}

void SearchPanel::onReplaceAll()
{
    QString keyword = m_searchInput->text().trimmed();
    QString replacement = m_replaceInput ? m_replaceInput->text() : QString();

    if (keyword.isEmpty()) return;

    int totalReplaced = 0;
    int fileCount = 0;

    // V1.9: 遍历工作区所有文件夹
    QStringList searchDirs = m_workspaceFolders.isEmpty() ?
        QStringList{QCoreApplication::applicationDirPath() + QStringLiteral("/Files")} :
        m_workspaceFolders;

    QRegularExpression regex;
    if (m_chkRegex && m_chkRegex->isChecked()) {
        regex.setPattern(keyword);
        if (!m_chkCaseSensitive || !m_chkCaseSensitive->isChecked()) {
            regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        }
        if (!regex.isValid()) return;
    }

    Qt::CaseSensitivity cs = (m_chkCaseSensitive && m_chkCaseSensitive->isChecked()) ?
        Qt::CaseSensitive : Qt::CaseInsensitive;

    for (const QString& searchDir : searchDirs) {
        QDir dir(searchDir);
        if (!dir.exists()) continue;

        QDirIterator it(searchDir, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString filePath = it.next();
            QFileInfo fi(filePath);
            QString suffix = fi.suffix().toLower();
            if (suffix == QStringLiteral("exe") || suffix == QStringLiteral("dll") ||
                suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") ||
                suffix == QStringLiteral("ico") || suffix == QStringLiteral("zip"))
                continue;

            if (!matchesFileFilter(fi.fileName())) continue;

            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QTextStream in(&file);
            QString content = in.readAll();
            file.close();

            QString newContent = content;
            int replaced = 0;

            if (m_chkRegex && m_chkRegex->isChecked()) {
                int offset = 0;
                QRegularExpressionMatch match;
                while ((match = regex.match(newContent, offset)).hasMatch()) {
                    newContent.replace(match.capturedStart(), match.capturedLength(), replacement);
                    offset = match.capturedStart() + replacement.length();
                    replaced++;
                }
            } else {
                int idx = 0;
                while ((idx = newContent.indexOf(keyword, idx, cs)) >= 0) {
                    newContent.replace(idx, keyword.length(), replacement);
                    idx += replacement.length();
                    replaced++;
                }
            }

            if (replaced > 0) {
                if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                    QTextStream out(&file);
                    out << newContent;
                    file.close();
                    totalReplaced += replaced;
                    fileCount++;
                }
            }
        }
    }

    m_searchResults->clear();
    m_searchResults->addItem(tr("已替换 %1 处，涉及 %2 个文件")
        .arg(totalReplaced).arg(fileCount));

    if (!keyword.isEmpty()) {
        onSearchTriggered();
    }
}

QList<SymbolEntry> scanFileSymbols(const QString& filePath)
{
    // V1.9: 正则扫描单个文件的符号定义
    QList<SymbolEntry> result;

    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();

    QRegularExpression re;
    if (suffix == QStringLiteral("py")) {
        re.setPattern(QStringLiteral("^(\\s*)(class|def)\\s+(\\w+)"));
    } else if (suffix == QStringLiteral("js") || suffix == QStringLiteral("ts")) {
        re.setPattern(QStringLiteral("^(\\s*)(function|class|const|let|var)\\s+(\\w+)"));
    } else if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("h") ||
               suffix == QStringLiteral("hpp") || suffix == QStringLiteral("cc") ||
               suffix == QStringLiteral("cxx") || suffix == QStringLiteral("c")) {
        re.setPattern(QStringLiteral("^(\\s*)(class|struct|enum|namespace|void|int|bool|double|float|QString|auto|inline|static)\\s+(\\w+)"));
    } else if (suffix == QStringLiteral("md")) {
        re.setPattern(QStringLiteral("^(#{1,6})\\s+(.+)$"));
    } else {
        return result;  // 不支持的类型
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return result;
    QTextStream in(&file);
    QStringList lines = in.readAll().split(QLatin1Char('\n'));
    file.close();

    for (int i = 0; i < lines.size(); ++i) {
        auto m = re.match(lines[i]);
        if (m.hasMatch()) {
            QString keyword = m.captured(2);
            QString name = m.captured(3);
            QString icon = QStringLiteral("•");

            if (keyword == QStringLiteral("class") || keyword == QStringLiteral("struct"))
                icon = QStringLiteral("C");
            else if (keyword == QStringLiteral("def") || keyword == QStringLiteral("function"))
                icon = QStringLiteral("f");
            else if (keyword == QStringLiteral("enum"))
                icon = QStringLiteral("E");
            else if (keyword == QStringLiteral("namespace"))
                icon = QStringLiteral("N");

            result.append({name, i, icon});
        }
    }

    return result;
}

void SearchPanel::performSymbolSearch(const QString& keyword)
{
    // V1.9: 全局符号搜索 — 遍历工作区所有文件，扫描符号定义
    m_searchResults->clear();

    QStringList searchDirs = m_workspaceFolders.isEmpty() ?
        QStringList{QCoreApplication::applicationDirPath() + QStringLiteral("/Files")} :
        m_workspaceFolders;

    Qt::CaseSensitivity cs = (m_chkCaseSensitive && m_chkCaseSensitive->isChecked()) ?
        Qt::CaseSensitive : Qt::CaseInsensitive;

    int totalFound = 0;

    for (const QString& searchDir : searchDirs) {
        QDirIterator it(searchDir, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString filePath = it.next();
            QFileInfo fi(filePath);
            QString suffix = fi.suffix().toLower();

            // 跳过二进制文件
            if (suffix == QStringLiteral("exe") || suffix == QStringLiteral("dll") ||
                suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") ||
                suffix == QStringLiteral("ico") || suffix == QStringLiteral("zip"))
                continue;

            if (!matchesFileFilter(fi.fileName())) continue;

            // 扫描符号
            auto symbols = scanFileSymbols(filePath);
            for (const auto& sym : symbols) {
                // 匹配符号名
                bool match = keyword.isEmpty() ||
                    sym.name.contains(keyword, cs);
                if (!match) continue;

                // 添加到结果列表
                QString displayText = QStringLiteral("%1 %2  —  %3:%4")
                    .arg(sym.icon, sym.name, fi.fileName())
                    .arg(sym.line + 1);

                auto* item = new QListWidgetItem(displayText, m_searchResults);
                item->setToolTip(filePath);
                // 存储跳转信息：UserRole=filePath, UserRole+1=line(0-based), UserRole+2=类型
                item->setData(Qt::UserRole, filePath);
                item->setData(Qt::UserRole + 1, sym.line);
                item->setData(Qt::UserRole + 2, QStringLiteral("symbol"));  // 标记为符号搜索
                totalFound++;

                if (totalFound >= 500) {
                    m_searchResults->addItem(tr("... 结果过多，仅显示前 500 项"));
                    return;
                }
            }
        }
    }

    if (totalFound == 0) {
        m_searchResults->addItem(tr("未找到匹配符号"));
    } else {
        LOG_DEBUG("[SearchPanel] 符号搜索完成: " << totalFound << " 个结果");
    }
}

void SearchPanel::onResultDoubleClicked(QListWidgetItem* item)
{
    if (!item) return;
    QString filePath = item->data(Qt::UserRole).toString();
    if (filePath.isEmpty()) return;

    // V1.9: 统一跳转逻辑 — 打开文件并跳转到指定行
    int line = item->data(Qt::UserRole + 1).toInt();  // 0-based
    QString resultType = item->data(Qt::UserRole + 2).toString();

    if (resultType == QStringLiteral("symbol") || resultType == QStringLiteral("text")) {
        // 符号搜索或文本搜索结果：打开文件并跳转到行
        emit fileOpenRequested(filePath);
        emit locateRequested(filePath, line, 0);
    } else {
        // 兼容旧结果（无类型标记）：仅打开文件
        emit fileOpenRequested(filePath);
    }
}
