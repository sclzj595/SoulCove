#ifndef DEFAULTUILIBRARY_H
#define DEFAULTUILIBRARY_H

#include "interfaces/ui/IUiLibrary.h"
#include <QString>

class QWidget;

/// @brief 默认UI库实现 — 封装当前项目的自定义UI组件
/// 实现IUiLibrary接口，使UI层可通过工厂模式创建组件
/// 未来可替换为 QCustomUi / Qt-Material 等三方库实现
class DefaultUiLibrary : public IUiLibrary
{
public:
    DefaultUiLibrary() = default;
    ~DefaultUiLibrary() override = default;

    // === IUiLibrary 接口实现 ===
    QString name() const override { return QStringLiteral("SoulCove-Native"); }

    bool initialize() override;
    void applyTheme(const QString& themeKey) override;

    QWidget* createFramelessWindow(QWidget* parent = nullptr) override;
    QWidget* createModernButton(const QString& text, QWidget* parent = nullptr) override;
};

#endif // DEFAULTUILIBRARY_H
