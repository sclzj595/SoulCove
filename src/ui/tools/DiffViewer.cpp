#include "ui/tools/DiffViewer.h"
#include "core/config/ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QFile>
#include <QDebug>

// ========== 构造函数 ==========

DiffViewer::DiffViewer(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

// ========== UI 构建 ==========

void DiffViewer::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === 顶部工具栏 ===
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("diffToolbar"));
    toolbar->setFixedHeight(32);

    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 0, 4, 0);
    toolbarLayout->setSpacing(8);

    // 左侧标签
    auto* leftLabel = new QLabel(tr("原始版本"), toolbar);
    leftLabel->setObjectName(QStringLiteral("diffSideLabel"));

    // 分隔符
    auto* sep = new QLabel(QStringLiteral("│"), toolbar);
    // 分隔符颜色跟随主题
    QString sepColor = ThemeManager::instance().currentPalette().fgDisabled.name(QColor::HexRgb);
    sep->setStyleSheet(QStringLiteral("color: %1; font-weight: bold; padding: 0 4px;").arg(sepColor));

    // 右侧标签
    auto* rightLabel = new QLabel(tr("修改版本"), toolbar);
    rightLabel->setObjectName(QStringLiteral("diffSideLabel"));

    // 统计信息
    m_statsLabel = new QLabel(tr("+0 ~0 -0"), toolbar);
    m_statsLabel->setObjectName(QStringLiteral("diffStatsLabel"));

    // 弹簧
    toolbarLayout->addWidget(leftLabel);
    toolbarLayout->addWidget(sep);
    toolbarLayout->addWidget(rightLabel);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_statsLabel);

    // 关闭按钮
    auto* btnClose = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"), toolbar);  // ✕
    btnClose->setFixedSize(24, 24);
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setToolTip(tr("关闭对比视图"));
    btnClose->setObjectName(QStringLiteral("panelCloseBtn"));
    connect(btnClose, &QPushButton::clicked, this, &DiffViewer::diffClosed);
    toolbarLayout->addWidget(btnClose);

    mainLayout->addWidget(toolbar);

    // === 并排显示区域 ===
    auto* splitWidget = new QWidget(this);
    auto* splitLayout = new QHBoxLayout(splitWidget);
    splitLayout->setContentsMargins(0, 0, 0, 0);
    splitLayout->setSpacing(1);

    // 左侧：原始版本
    m_leftArea = new QScrollArea(splitWidget);
    m_leftArea->setObjectName(QStringLiteral("diffScrollArea"));
    m_leftArea->setWidgetResizable(true);
    m_leftArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_leftEdit = new QTextEdit(m_leftArea);
    m_leftEdit->setReadOnly(true);
    m_leftEdit->setLineWrapMode(QTextEdit::NoWrap);
    m_leftEdit->setObjectName(QStringLiteral("diffTextEdit"));
    m_leftArea->setWidget(m_leftEdit);

    // 右侧：修改版本
    m_rightArea = new QScrollArea(splitWidget);
    m_rightArea->setObjectName(QStringLiteral("diffScrollArea"));
    m_rightArea->setWidgetResizable(true);
    m_rightArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_rightEdit = new QTextEdit(m_rightArea);
    m_rightEdit->setReadOnly(true);
    m_rightEdit->setLineWrapMode(QTextEdit::NoWrap);
    m_rightEdit->setObjectName(QStringLiteral("diffTextEdit"));
    m_rightArea->setWidget(m_rightEdit);

    splitLayout->addWidget(m_leftArea, 1);
    splitLayout->addWidget(m_rightArea, 1);

    mainLayout->addWidget(splitWidget, 1);

    // 同步滚动：左侧滚动 → 右侧跟随
    connect(m_leftArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        if (m_rightArea)
            m_rightArea->verticalScrollBar()->setValue(value);
    });

    // 同步滚动：右侧滚动 → 左侧跟随
    connect(m_rightArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        if (m_leftArea)
            m_leftArea->verticalScrollBar()->setValue(value);
    });
}

// ========== 设置对比内容 ==========

void DiffViewer::setDiffContent(const QString& originalText, const QString& modifiedText,
                                 const QString& originalLabel,
                                 const QString& modifiedLabel)
{
    QStringList origLines = originalText.split(QLatin1Char('\n'));
    QStringList modLines = modifiedText.split(QLatin1Char('\n'));

    // 计算差异
    QList<DiffLine> diffLines = computeDiff(origLines, modLines);

    // 重置统计
    m_addedLines = 0;
    m_removedLines = 0;
    m_modifiedLines = 0;

    // 构建左右两侧的 HTML 显示内容
    QString leftHtml;
    QString rightHtml;

    leftHtml += QStringLiteral("<body style='font-family: Consolas, \"Courier New\", monospace; font-size: 13px; margin:0;padding:4px;'>");
    rightHtml += QStringLiteral("<body style='font-family: Consolas, \"Courier New\", monospace; font-size: 13px; margin:0;padding:4px;'>");

    for (const auto& dl : diffLines) {
        QString escOrig = dl.originalText.toHtmlEscaped();
        QString escMod = dl.modifiedText.toHtmlEscaped();

        switch (dl.type) {
        case DiffLine::Unchanged:
            leftHtml += QStringLiteral("<div style='white-space:pre; color:#cccccc;'>%1</div>\n").arg(escOrig);
            rightHtml += QStringLiteral("<div style='white-space:pre; color:#cccccc;'>%1</div>\n").arg(escMod);
            break;
        case DiffLine::Added:
            ++m_addedLines;
            leftHtml += QStringLiteral("<div style='white-space:pre; color:#666666;'>&nbsp;</div>\n");
            rightHtml += QStringLiteral("<div style='white-space:pre; background-color:%1; color:#b4e699;'>+ %2</div>\n")
                .arg(COLOR_ADDED).arg(escMod);
            break;
        case DiffLine::Removed:
            ++m_removedLines;
            leftHtml += QStringLiteral("<div style='white-space:pre; background-color:%1; color:#f48771;'>- %2</div>\n")
                .arg(COLOR_REMOVED).arg(escOrig);
            rightHtml += QStringLiteral("<div style='white-space:pre; color:#666666;'>&nbsp;</div>\n");
            break;
        case DiffLine::Modified:
            ++m_modifiedLines;
            leftHtml += QStringLiteral("<div style='white-space:pre; background-color:%1; color:#f48771;'>~ %2</div>\n")
                .arg(COLOR_MODIFIED).arg(escOrig);
            rightHtml += QStringLiteral("<div style='white-space:pre; background-color:%1; color:#dcdcaa;'>~ %2</div>\n")
                .arg(COLOR_MODIFIED).arg(escMod);
            break;
        }
    }

    leftHtml += QStringLiteral("</body>");
    rightHtml += QStringLiteral("</body>");

    m_leftEdit->setHtml(leftHtml);
    m_rightEdit->setHtml(rightHtml);

    // 更新统计标签
    m_statsLabel->setText(
        tr("+%1 ~%2 -%3").arg(m_addedLines).arg(m_modifiedLines).arg(m_removedLines));
}

// ========== 从文件加载对比 ==========

void DiffViewer::loadFileDiff(const QString& originalPath, const QString& modifiedPath)
{
    QFile origFile(originalPath);
    QFile modFile(modifiedPath);
    QString origText, modText;

    if (origFile.open(QIODevice::ReadOnly | QIODevice::Text))
        origText = QString::fromUtf8(origFile.readAll());
    else
        origText = tr("（无法读取文件）");

    if (modFile.open(QIODevice::ReadOnly | QIODevice::Text))
        modText = QString::fromUtf8(modFile.readAll());
    else
        modText = tr("（无法读取文件）");

    setDiffContent(origText, modText,
                   QFileInfo(originalPath).fileName(),
                   QFileInfo(modifiedPath).fileName());
}

// ========== LCS 差异算法 ==========

QList<DiffViewer::DiffLine> DiffViewer::computeDiff(const QStringList& origLines, const QStringList& modLines)
{
    int n = origLines.size();
    int m = modLines.size();

    // 边界情况：任一为空
    if (n == 0 && m == 0) return {};
    if (n == 0) {
        QList<DiffLine> result;
        for (int i = 0; i < m; ++i) {
            DiffLine dl;
            dl.type = DiffLine::Added;
            dl.originalLineNo = -1;
            dl.modifiedLineNo = i + 1;
            dl.originalText.clear();
            dl.modifiedText = modLines[i];
            result.append(dl);
        }
        return result;
    }
    if (m == 0) {
        QList<DiffLine> result;
        for (int i = 0; i < n; ++i) {
            DiffLine dl;
            dl.type = DiffLine::Removed;
            dl.originalLineNo = i + 1;
            dl.modifiedLineNo = -1;
            dl.originalText = origLines[i];
            dl.modifiedText.clear();
            result.append(dl);
        }
        return result;
    }

    // 构建 LCS 长度表（动态规划）
    QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));

    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            if (origLines[i - 1] == modLines[j - 1])
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = qMax(dp[i - 1][j], dp[i][j - 1]);
        }
    }

    // 回溯构建差异列表
    QList<DiffLine> result;
    int i = n, j = m;

    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && origLines[i - 1] == modLines[j - 1]) {
            // 匹配行
            DiffLine dl;
            dl.type = DiffLine::Unchanged;
            dl.originalLineNo = i;
            dl.modifiedLineNo = j;
            dl.originalText = origLines[i - 1];
            dl.modifiedText = modLines[j - 1];
            result.prepend(dl);
            --i;
            --j;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            // 新增行
            DiffLine dl;
            dl.type = DiffLine::Added;
            dl.originalLineNo = -1;
            dl.modifiedLineNo = j;
            dl.originalText.clear();
            dl.modifiedText = modLines[j - 1];
            result.prepend(dl);
            --j;
        } else if (i > 0) {
            // 删除行
            DiffLine dl;
            dl.type = DiffLine::Removed;
            dl.originalLineNo = i;
            dl.modifiedLineNo = -1;
            dl.originalText = origLines[i - 1];
            dl.modifiedText.clear();
            result.prepend(dl);
            --i;
        }
    }

    // 后处理：将相邻的 Added+Removed 对合并为 Modified
    for (int k = 0; k < result.size() - 1; ++k) {
        if ((result[k].type == DiffLine::Removed && result[k + 1].type == DiffLine::Added) ||
            (result[k].type == DiffLine::Added && result[k + 1].type == DiffLine::Removed)) {

            // 确保 Removed 在前，Added 在后
            if (result[k].type == DiffLine::Added) {
                qSwap(result[k], result[k + 1]);
            }

            result[k].type = DiffLine::Modified;
            result[k + 1].type = DiffLine::Modified;
            ++k;  // 跳过下一对
        }
    }

    return result;
}
