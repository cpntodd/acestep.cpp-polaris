#pragma once

#include <QObject>
#include <QString>

class AppSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QString theme() const { return m_theme; }
    void setTheme(const QString &theme);

signals:
    void themeChanged();

private:
    QString m_theme;
};
