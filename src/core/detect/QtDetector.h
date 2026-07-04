#ifndef QTDETECTOR_H
#define QTDETECTOR_H

#include <QString>
#include <QList>

/// @brief Qt 安装信息结构体（P1 C05-3）
/// 描述一个被检测到的 Qt 安装（官方安装器布局：<QtRoot>/<版本>/<编译器>）
struct QtInstallation
{
    QString prefixPath;   ///< Qt 安装前缀，如 C:/Qt/6.5.3/mingw_64
    QString version;      ///< Qt 版本号，如 6.5.3
    QString compiler;     ///< 编译器标识，如 mingw_64 / msvc2019_64
    QString cmakeDir;     ///< Qt6 CMake 配置目录，如 C:/Qt/6.5.3/mingw_64/lib/cmake/Qt6

    /// 转换为单行可读字符串（用于列表展示）
    QString displayText() const;
};

/// @brief 系统 Qt 安装检测器（P1 C05-3）
/// 静态工具类，扫描常见路径下的 Qt 安装并验证 Qt6Config.cmake 存在性。
/// 检测结果注入到构建设置页的「检测系统 Qt」按钮。
class QtDetector
{
public:
    /// 检测所有 Qt 安装
    /// Windows: 遍历常见盘符 (C/D/E) 下的 Qt 目录，查找 <QtRoot>/<版本>/<编译器> 模式
    /// 验证 lib/cmake/Qt6/Qt6Config.cmake（或 Qt5/Qt5Config.cmake）存在
    static QList<QtInstallation> detectAll();

    /// 从环境变量检测 Qt 安装
    /// 依次检查 Qt6_DIR、CMAKE_PREFIX_PATH 环境变量
    /// @return 若环境变量指向有效 Qt 安装则返回对应信息，否则返回空 QtInstallation（prefixPath 为空）
    static QtInstallation detectFromEnv();

private:
    /// 验证指定前缀路径是否为有效 Qt 安装，并填充 QtInstallation 信息
    /// @param prefix 候选前缀路径（如 C:/Qt/6.5.3/mingw_64）
    /// @return 有效则返回填充后的信息，无效返回空 QtInstallation
    static QtInstallation validatePrefix(const QString& prefix);

    /// 从版本目录与编译器目录名推断完整前缀并验证
    static QtInstallation probeCompilerDir(const QString& versionDir, const QString& compilerName);

    /// 从目录路径解析版本号（目录名形如 6.5.3）
    static QString parseVersion(const QString& versionDirName);
};

#endif // QTDETECTOR_H
