#include "ui/tools/RegexTester.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QClipboard>
#include <QApplication>
#include <QScrollBar>

// ========== 构造函数 ==========

RegexTester::RegexTester(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(6);

    // === 正则输入行 ===
    auto* regexLayout = new QHBoxLayout();
    regexLayout->setSpacing(4);

    auto* regexLabel = new QLabel(tr("正则:"), this);
    m_regexEdit = new QLineEdit(this);
    m_regexEdit->setObjectName(QStringLiteral("regexInput"));
    m_regexEdit->setPlaceholderText(tr("输入正则表达式..."));
    regexLayout->addWidget(regexLabel);
    regexLayout->addWidget(m_regexEdit, 1);

    mainLayout->addLayout(regexLayout);

    // === 选项行 ===
    auto* optionLayout = new QHBoxLayout();
    optionLayout->setSpacing(12);

    m_caseSensitive = new QCheckBox(tr("区分大小写"), this);
    m_multiline = new QCheckBox(tr("多行模式"), this);
    m_dotAll = new QCheckBox(tr(". 匹配换行"), this);

    auto* syntaxLabel = new QLabel(tr("语法:"), this);
    m_patternSyntax = new QComboBox(this);
    m_patternSyntax->addItem(tr("RegExp"), 0);  // NoPatternOption
    m_patternSyntax->addItem(tr("Wildcard"), 1); // WildcardPatternOption
    m_patternSyntax->addItem(tr("FixedString"), 2); // 转义为字面量

    optionLayout->addWidget(m_caseSensitive);
    optionLayout->addWidget(m_multiline);
    optionLayout->addWidget(m_dotAll);
    optionLayout->addStretch();
    optionLayout->addWidget(syntaxLabel);
    optionLayout->addWidget(m_patternSyntax);

    mainLayout->addLayout(optionLayout);

    // === 测试文本区域 ===
    auto* testLabel = new QLabel(tr("测试文本:"), this);
    mainLayout->addWidget(testLabel);

    m_testTextEdit = new QTextEdit(this);
    m_testTextEdit->setObjectName(QStringLiteral("regexTestEdit"));
    m_testTextEdit->setPlaceholderText(tr("输入或粘贴要匹配的文本..."));
    m_testTextEdit->setMinimumHeight(120);
    mainLayout->addWidget(m_testTextEdit, 1);  // stretch=1

    // === 结果区域 ===
    auto* resultLabel = new QLabel(tr("匹配结果:"), this);
    mainLayout->addWidget(resultLabel);

    m_resultTextEdit = new QTextEdit(this);
    m_resultTextEdit->setObjectName(QStringLiteral("regexResultEdit"));
    m_resultTextEdit->setReadOnly(true);
    m_resultTextEdit->setMinimumHeight(120);
    mainLayout->addWidget(m_resultTextEdit, 1);  // stretch=1

    // === 状态栏 + 按钮 ===
    auto* statusLayout = new QHBoxLayout();
    statusLayout->setSpacing(4);

    m_statusLabel = new QLabel(tr("就绪"), this);
    m_statusLabel->setObjectName(QStringLiteral("regexStatusLabel"));

    m_btnCopy = new QPushButton(tr("复制全部匹配"), this);
    m_btnCopy->setObjectName(QStringLiteral("gitActionBtn"));

    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_btnCopy);

    mainLayout->addLayout(statusLayout);

    // === 防抖定时器（300ms 延迟自动重算）===
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(300);
    connect(m_debounceTimer, &QTimer::timeout, this, &RegexTester::updateResults);

    // === 信号连接 ===
    connect(m_regexEdit, &QLineEdit::textChanged, this, [this]() {
        m_debounceTimer->start();  // 防抖
    });
    connect(m_testTextEdit, &QTextEdit::textChanged, this, [this]() {
        m_debounceTimer->start();  // 防抖
    });
    connect(m_caseSensitive, &QCheckBox::toggled, this, &RegexTester::onOptionChanged);
    connect(m_multiline, &QCheckBox::toggled, this, &RegexTester::onOptionChanged);
    connect(m_dotAll, &QCheckBox::toggled, this, &RegexTester::onOptionChanged);
    connect(m_patternSyntax, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RegexTester::onOptionChanged);
    connect(m_btnCopy, &QPushButton::clicked, this, &RegexTester::onCopyMatchedText);
}

void RegexTester::setTestText(const QString& text)
{
    m_testTextEdit->setPlainText(text);
}

// ========== 核心匹配逻辑 ==========

void RegexTester::updateResults()
{
    QString pattern = m_regexEdit->text();
    QString testText = m_testTextEdit->toPlainText();

    if (pattern.isEmpty()) {
        m_resultTextEdit->clear();
        m_statusLabel->setText(tr("就绪"));
        return;
    }

    // 构建选项
    QRegularExpression::PatternOptions options = QRegularExpression::PatternOption::NoPatternOption;

    if (!m_caseSensitive->isChecked())
        options |= QRegularExpression::CaseInsensitiveOption;
    if (m_multiline->isChecked())
        options |= QRegularExpression::MultilineOption;
    if (m_dotAll->isChecked())
        options |= QRegularExpression::DotMatchesEverythingOption;

    // 语法类型
    int syntaxData = m_patternSyntax->currentData().toInt();
    if (syntaxData == 1) {
        // Wildcard: 将通配符模式转换为正则表达式
        pattern = QRegularExpression::wildcardToRegularExpression(pattern);
    } else if (syntaxData == 2) {
        // FixedString: 转义正则特殊字符，作为字面量匹配
        pattern = QRegularExpression::escape(pattern);
    }
    // syntaxData == 0: 标准 RegExp（默认 NoPatternOption）

    QRegularExpression regex(pattern, options);

    // 检查语法有效性
    if (!regex.isValid()) {
        QString errorStr = regex.errorString();
        m_resultTextEdit->setHtml(
            QStringLiteral("<span style='color:#ff4444;font-weight:bold;'>")
            + tr("语法错误: ")
            + errorStr.toHtmlEscaped()
            + QStringLiteral("</span>"));
        m_statusLabel->setText(tr("❌ 语法错误"));
        return;
    }

    // 执行全局匹配
    QRegularExpressionMatchIterator it = regex.globalMatch(testText);

    // 构建富文本结果
    QStringList capturedTexts;
    QString htmlResult;
    htmlResult += QStringLiteral("<div style='font-family:Consolas,\"Courier New\",monospace;font-size:13px;'>");

    int matchIndex = 0;
    while (it.hasNext()) {
        ++matchIndex;
        QRegularExpressionMatch match = it.next();

        int start = match.capturedStart();
        int end = match.capturedEnd();
        QString matched = match.captured();

        // 获取行列号
        int lineNum = testText.left(start).count(QLatin1Char('\n')) + 1;
        int colNum = start - testText.lastIndexOf(QLatin1Char('\n'), start);

        htmlResult += QStringLiteral("<div style='margin:2px 0;padding:3px 6px;"
                                      "background-color:#2d2d2d;border-radius:3px;'>");
        htmlResult += QStringLiteral("<span style='color:#569cd6;'>[")
                      + QString::number(matchIndex)
                      + QStringLiteral("]</span> ");
        htmlResult += QStringLiteral("<span style='color:#9cdcfe;'>")
                      + tr("第%1行,第%2列").arg(lineNum).arg(colNum)
                      + QStringLiteral("</span>");
        htmlResult += QStringLiteral(": ");

        // 高亮匹配文字（黄色背景）
        htmlResult += QStringLiteral("<span style='background-color:#3c3c00;"
                                      "color:#dcdcaa;padding:1px 3px;"
                                      "border-radius:2px;'>'");
        htmlResult += matched.toHtmlEscaped();
        htmlResult += QStringLiteral("'</span>");

        // 显示捕获组
        if (match.lastCapturedIndex() > 0) {
            htmlResult += QStringLiteral(" <span style='color:#888888;'>(");
            for (int g = 1; g <= match.lastCapturedIndex(); ++g) {
                if (g > 1) htmlResult += QStringLiteral(", ");
                htmlResult += QStringLiteral("$") + QString::number(g)
                              + QStringLiteral("=")
                              + match.captured(g).toHtmlEscaped();
            }
            htmlResult += QStringLiteral(")</span>");
        }

        htmlResult += QStringLiteral("</div>");

        capturedTexts.append(matched);
    }

    htmlResult += QStringLiteral("</div>");

    if (matchIndex == 0) {
        m_resultTextEdit->setHtml(
            QStringLiteral("<div style='color:#888888;padding:10px;'>")
            + tr("未找到匹配项")
            + QStringLiteral("</div>"));
        m_statusLabel->setText(tr("✅ 找到 0 个匹配"));
    } else {
        m_resultTextEdit->setHtml(htmlResult);
        // 滚动到顶部
        m_resultTextEdit->verticalScrollBar()->setValue(0);
        m_statusLabel->setText(tr("✅ 找到 %1 个匹配").arg(matchIndex));
    }
}

// ========== 槽函数 ==========

void RegexTester::onRegexChanged()
{
    updateResults();
}

void RegexTester::onOptionChanged()
{
    updateResults();
}

void RegexTester::onCopyMatchedText()
{
    // 从结果中提取所有匹配文本并复制到剪贴板
    QString pattern = m_regexEdit->text();
    QString testText = m_testTextEdit->toPlainText();

    if (pattern.isEmpty() || testText.isEmpty()) return;

    QRegularExpression::PatternOptions options = QRegularExpression::PatternOption::NoPatternOption;
    if (!m_caseSensitive->isChecked())
        options |= QRegularExpression::CaseInsensitiveOption;
    if (m_multiline->isChecked())
        options |= QRegularExpression::MultilineOption;
    if (m_dotAll->isChecked())
        options |= QRegularExpression::DotMatchesEverythingOption;

    int syntaxData = m_patternSyntax->currentData().toInt();
    if (syntaxData == 1) {
        pattern = QRegularExpression::wildcardToRegularExpression(pattern);
    } else if (syntaxData == 2) {
        pattern = QRegularExpression::escape(pattern);
    }

    QRegularExpression regex(pattern, options);
    if (!regex.isValid()) return;

    QStringList results;
    QRegularExpressionMatchIterator it = regex.globalMatch(testText);
    while (it.hasNext()) {
        results.append(it.next().captured());
    }

    if (!results.isEmpty()) {
        QApplication::clipboard()->setText(results.join(QStringLiteral("\n")));
        m_statusLabel->setText(tr("✅ 已复制 %1 个匹配项").arg(results.size()));
    }
}
