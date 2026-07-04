#include "UIFactory.h"
#include "core/markdown/MaddyParser.h"
#include "core/markdown/MarkdownParser.h"
#include "core/config/DefaultUiLibrary.h"
#include "interfaces/ui/IUiLibrary.h"
#include "interfaces/markdown/IMarkdownParser.h"

// 静态成员初始化
std::unique_ptr<IUiLibrary> UIFactory::m_uiLib;

// 确保UI库实例已初始化（懒加载单例模式）
IUiLibrary& UIFactory::ensureUiLib()
{
    if (!m_uiLib) {
        m_uiLib = std::make_unique<DefaultUiLibrary>();
        m_uiLib->initialize();
    }
    return *m_uiLib;
}

QPushButton* UIFactory::createIconButton(QWidget* parent, const QString& normalIcon, const QString& hoverIcon, int size)
{
    auto* btn = new QPushButton(parent);
    btn->setFixedSize(size, size);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName(QStringLiteral("iconButton"));
    btn->setProperty("iconButton", true);
    btn->setStyleSheet(
        QStringLiteral("QPushButton { border: none; border-image: url(%1); background: transparent; }"
                       "QPushButton:hover { border-image: url(%2); }"
                       "QPushButton:pressed { border-image: url(%2); }")
            .arg(normalIcon)
            .arg(hoverIcon)
    );
    return btn;
}

QLabel* UIFactory::createStatusLabel(QWidget* parent, const QString& text)
{
    auto* label = new QLabel(text, parent);
    return label;
}

QComboBox* UIFactory::createEncodingComboBox(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    combo->setMinimumWidth(100);
    combo->addItems({QStringLiteral("UTF-8"), QStringLiteral("UTF-16"),
                     QStringLiteral("GBK"), QStringLiteral("ANSI"), QStringLiteral("GB2312")});
    return combo;
}

QWidget* UIFactory::createToolBar(QWidget* parent)
{
    auto* bar = new QWidget(parent);
    bar->setFixedHeight(36);   // 与标题栏同高，紧凑
    bar->setObjectName(QStringLiteral("toolBarWidget"));
    return bar;
}

QWidget* UIFactory::createStatusBar(QWidget* parent)
{
    auto* bar = new QWidget(parent);
    bar->setFixedHeight(24);
    bar->setObjectName(QStringLiteral("statusBar"));
    return bar;
}

IMarkdownParser* UIFactory::createMarkdownParser(const QString& type)
{
    if (type == QStringLiteral("simple"))
        return new MarkdownParser();
    // 默认返回 maddy 解析器
    return new MaddyParser();
}

QWidget* UIFactory::createFramelessWindow(QWidget* parent)
{
    // 通过 IUiLibrary 接口创建，支持未来替换为其他 UI 库实现
    return ensureUiLib().createFramelessWindow(parent);
}

QWidget* UIFactory::createModernButton(const QString& text, QWidget* parent)
{
    // 通过 IUiLibrary 接口创建，支持未来替换为其他 UI 库实现
    return ensureUiLib().createModernButton(text, parent);
}

IUiLibrary& UIFactory::uiLibrary()
{
    return ensureUiLib();
}
