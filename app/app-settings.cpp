#include "app-settings.h"

#include <QSettings>

AppSettings::AppSettings(QObject *parent)
    : QObject(parent) {
    m_theme = QSettings().value(QStringLiteral("appearance/theme"), QStringLiteral("night")).toString();
}

void AppSettings::setTheme(const QString &theme) {
    const QString normalized = theme == QStringLiteral("amber") || theme == QStringLiteral("mono")
                                   ? theme
                                   : QStringLiteral("night");
    if (m_theme == normalized) return;
    m_theme = normalized;
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/theme"), m_theme);
    settings.sync();
    emit themeChanged();
}
