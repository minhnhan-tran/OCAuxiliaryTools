#pragma once
// Switchable icon sets. "classic" = the bundled PNGs under :/icon/. "material" =
// Material Symbols SVGs under :/icon-material/, tinted at runtime, so one file
// serves the dark, light and selected variants. Any icon without a Material
// equivalent falls back to the classic PNG.
#include <QApplication>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QString>

#include "theme.h"

extern QSettings Reg;

namespace IconSet {

inline QString& styleRef() {
  static QString s;
  return s;
}

// "classic" (default) or "material"; persisted as IconStyle in the INI file.
inline QString style() {
  QString& s = styleRef();
  if (s.isEmpty()) {
    s = Reg.value("IconStyle", "classic").toString();
    if (s != "material") s = "classic";
  }
  return s;
}

inline void setStyle(const QString& v) {
  styleRef() = (v == "material") ? "material" : "classic";
  Reg.setValue("IconStyle", styleRef());
}

struct Spec {
  const char* svg;
  const char* color;  // fixed semantic colour; nullptr = follow the theme
};

// classic base name -> Material Symbol
inline const QHash<QString, Spec>& specs() {
  static const QHash<QString, Spec> m = {
      {"save", {"save", nullptr}},          {"savetip", {"save", "#2f81f7"}},
      {"undo", {"undo", nullptr}},          {"redo", {"redo", nullptr}},
      {"find", {"search", nullptr}},        {"db", {"database", nullptr}},
      {"opendir", {"folder_open", nullptr}}, {"esp", {"hard_drive", nullptr}},
      {"um", {"system_update_alt", nullptr}}, {"ov", {"verified", nullptr}},
      {"overror", {"error", "#e5534b"}},    {"efi", {"deployed_code", nullptr}},
      {"postcheck", {"fact_check", nullptr}}, {"usbmap", {"usb", nullptr}},
      {"be", {"backup", nullptr}},          {"c", {"clear_all", nullptr}},
      {"rp", {"history", nullptr}},         {"ok", {"check_circle", "#3fb950"}},
      {"no", {"cancel", "#e5534b"}},        {"newver", {"new_releases", "#f0883e"}},
      // left sidebar: ACPI, Booter, DeviceProperties, Kernel, Misc, NVRAM, PlatformInfo, UEFI
      {"m1", {"developer_board", nullptr}}, {"m2", {"rocket_launch", nullptr}},
      {"m3", {"devices", nullptr}},         {"m4", {"memory", nullptr}},
      {"m5", {"tune", nullptr}},            {"m6", {"storage", nullptr}},
      {"m7", {"computer", nullptr}},        {"m8", {"terminal", nullptr}},
  };
  return m;
}

inline bool dark() {
  return Theme::isDark(qApp->palette().color(QPalette::Window).red());
}

// Renders :/icon-material/<svg>.svg tinted `color` at several sizes.
inline QIcon tinted(const QString& svg, const QColor& color) {
  static QHash<QString, QIcon> cache;
  const QString key = svg + color.name();
  const auto it = cache.constFind(key);
  if (it != cache.constEnd()) return *it;
  QIcon out;
  const QIcon src(":/icon-material/" + svg + ".svg");
  for (int sz : {16, 24, 32, 48, 64, 128}) {
    QPixmap pm = src.pixmap(QSize(sz, sz));
    if (pm.isNull()) continue;
    QPainter p(&pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pm.rect(), color);
    p.end();
    out.addPixmap(pm);
  }
  cache.insert(key, out);
  return out;
}

// Drop-in for QIcon(":/icon/<name>.png"): "<base>0" = dark-theme variant,
// "<base>sel" = selected variant.
inline QIcon icon(const QString& path) {
  if (style() != "material" || !path.startsWith(":/icon/") || !path.endsWith(".png"))
    return QIcon(path);
  QString name = path.mid(7, path.length() - 11);
  bool selected = false;
  bool darkVariant = false;
  if (name.endsWith("sel") && specs().contains(name.left(name.length() - 3))) {
    selected = true;
    name.chop(3);
  } else if (name.endsWith("0") && specs().contains(name.left(name.length() - 1))) {
    darkVariant = true;
    name.chop(1);
  }
  const auto it = specs().constFind(name);
  if (it == specs().constEnd()) return QIcon(path);
  QColor c;
  if (it->color) c = QColor(it->color);
  else if (selected) c = qApp->palette().color(QPalette::Highlight);
  else if (darkVariant || dark()) c = QColor("#d0d0d0");
  else c = QColor("#4a4f57");
  const QIcon r = tinted(it->svg, c);
  return r.isNull() ? QIcon(path) : r;
}

}  // namespace IconSet
