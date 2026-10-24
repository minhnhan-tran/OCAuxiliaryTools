#ifndef THEME_H
#define THEME_H

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QHash>
#include <QPalette>
#include <QString>
#include <QStyle>

// Central dark/light theme helpers. "red" is the red channel of the window
// background (see MainWindow::paintEvent); below the threshold = dark mode.
namespace Theme {

constexpr int kDarkThreshold = 55;

inline bool isDark(int red) { return red < kDarkThreshold; }

inline QString loadQss(const QString& resPath) {
  QFile f(resPath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
  return QString::fromUtf8(f.readAll());
}

inline QString listStyle(int red) {
  return loadQss(isDark(red) ? ":/theme/list-dark.qss"
                             : ":/theme/list-light.qss");
}

// Icons come in light-theme (name.png) and dark-theme (name0.png) variants.
inline QString icon(const QString& base, int red) {
  return QString(":/icon/%1%2.png").arg(base, isDark(red) ? "0" : "");
}

// Material colour tokens; one set per mode. Names match @{token} in
// theme/material.qss.
inline QHash<QString, QString> tokens(bool dark) {
  if (dark)
    return {{"surface", "#2C2C2E"},       {"border", "#424246"},
            {"text", "#E6E6E6"},          {"textSecondary", "#A0A0A5"},
            {"accent", "#64B5F6"},        {"accentHover", "#90CAF9"},
            {"accentPressed", "#42A5F5"}, {"onAccent", "#0D1B2A"},
            {"hover", "rgba(255,255,255,20)"}, {"scroll", "#5F6368"},
            {"disabledBg", "#3A3A3C"},    {"disabledText", "#7A7A7F"}};
  return {{"surface", "#FFFFFF"},       {"border", "#DADCE0"},
          {"text", "#212121"},          {"textSecondary", "#5F6368"},
          {"accent", "#1976D2"},        {"accentHover", "#1565C0"},
          {"accentPressed", "#0D47A1"}, {"onAccent", "#FFFFFF"},
          {"hover", "rgba(0,0,0,15)"},  {"scroll", "#BDBDBD"},
          {"disabledBg", "#E0E0E0"},    {"disabledText", "#9E9E9E"}};
}

inline QString materialQss(bool dark) {
  QString qss = loadQss(":/theme/material.qss");
  const QHash<QString, QString> t = tokens(dark);
  for (auto it = t.constBegin(); it != t.constEnd(); ++it)
    qss.replace("@{" + it.key() + "}", it.value());
  return qss;
}

// Window red stays well below kDarkThreshold in the dark palette (34 < 55) and
// far above it in the light one, so MainWindow's dark-aware branches follow.
inline QPalette materialPalette(bool dark) {
  const QHash<QString, QString> t = tokens(dark);
  const QColor window = dark ? QColor(34, 34, 36) : QColor(250, 250, 250);
  const QColor base = dark ? QColor(24, 24, 26) : QColor(255, 255, 255);
  const QColor text(t["text"]), accent(t["accent"]), disabled(t["disabledText"]);
  QPalette p;
  p.setColor(QPalette::Window, window);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, base);
  p.setColor(QPalette::AlternateBase, window);
  p.setColor(QPalette::ToolTipBase, QColor(t["surface"]));
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, QColor(t["surface"]));
  p.setColor(QPalette::ButtonText, text);
  p.setColor(QPalette::BrightText, Qt::red);
  p.setColor(QPalette::Link, accent);
  p.setColor(QPalette::Highlight, accent);
  p.setColor(QPalette::HighlightedText, QColor(t["onAccent"]));
  p.setColor(QPalette::Disabled, QPalette::Text, disabled);
  p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
  p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
  return p;
}

// Applies the app-wide look (every window, dialog and tooltip inherits it).
//   material: Material palette + stylesheet (light or dark per `dark`)
//   dark only: plain dark palette on Fusion
//   neither:   restores the style/palette captured on the first call
// MainWindow::paintEvent notices the palette change and refreshes its own
// dark-aware widgets, so this works live.
inline void apply(bool dark, bool material) {
  static bool saved = false;
  static QString origStyle;
  static QPalette origPalette;
  static bool systemDark = false;
  if (!saved) {
    origStyle = qApp->style()->objectName();
    origPalette = qApp->palette();
    systemDark = isDark(origPalette.color(QPalette::Window).red());
    saved = true;
  }
  if (dark || material) {
    // Material alone must not override a dark OS appearance (the app followed
    // it before this theme existed), so it tracks the system unless Dark is on.
    const bool useDark = dark || (material && systemDark);
    qApp->setStyle("Fusion");
    qApp->setPalette(materialPalette(useDark));
    qApp->setStyleSheet(material ? materialQss(useDark) : QString());
  } else {
    qApp->setStyleSheet(QString());
    qApp->setStyle(origStyle);
    qApp->setPalette(origPalette);
  }
}

}  // namespace Theme

#endif  // THEME_H
