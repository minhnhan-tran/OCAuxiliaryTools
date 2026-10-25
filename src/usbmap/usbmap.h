#ifndef USBMAP_H
#define USBMAP_H

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// Native C++ port of the logic of corpnewt/USBMap (MIT, see LICENSE-USBMap.txt): discovers USB controllers/ports from
// `ioreg`, keeps the user's choices, and builds the USBMap.kext Info.plist.
// No GUI here, so it can be unit-tested against USBMap's own output.
namespace UsbMap {

struct Port {
  QString key;        // little-endian hex as ioreg prints it, e.g. "01000000"
  QString name, type, address;
  int connector = -1;      // UsbConnector from ioreg, -1 = unknown
  int enabled = -1;        // -1 = not decided yet, 0 = off, 1 = on
  int typeOverride = -1;   // -1 = none, else the connector type the user chose
  QString comment;         // user's nickname
  bool containsHub = false;
  QStringList items;       // names of devices seen on this port
};

struct Controller {
  QString key, name, type, address;
  QString parentName, pciDebug, acpiPath, ioservicePath;
  bool isHub = false;
  QString locationId;      // decimal, hubs only ("" = none)
  int indent = 0;          // discovery only
  QMap<QString, Port> ports;  // sorted by port key, like USBMap
};

typedef QList<Controller> ControllerList;

// ioreg text -> lines with wrapped "+-o" entries joined (USBMap.sanitize_ioreg).
QStringList sanitizeIoreg(const QString& text);

// Parse an `ioreg -l -p IOService -w0` dump. Returns false with `error` set when
// no USB controller is found.
bool discover(const QStringList& ioreg, ControllerList* out, QString* smbios,
              QString* error, bool mapHubs = false);

// Merge freshly discovered controllers into the saved ones, keeping what the
// user decided (enabled, type, nickname) and accumulating seen devices.
void merge(ControllerList* into, const ControllerList& from);

int enabledCount(const Controller& c);
// Apple's per-controller limit is 15 enabled ports.
const int kPortLimit = 15;

// Info.plist of USBMap.kext. `legacy` = AppleUSBMergeNub (macOS 10.11 - 10.14).
QByteArray infoPlist(const ControllerList& list, const QString& smbios, bool legacy,
                     QString* error);
// Writes <dir>/<USBMap|USBMapLegacy>.kext/Contents/Info.plist, returns the kext path.
QString writeKext(const QString& dir, const ControllerList& list, const QString& smbios,
                  bool legacy, QString* error);

QByteArray toJson(const ControllerList& list, const QString& smbios);
bool fromJson(const QByteArray& data, ControllerList* list, QString* smbios);

// helpers (exposed for tests)
QString checkHex(const QString& v);
QString hexSwap(const QString& v);   // "" when v has no hex digits
qint64 hexDec(const QString& v);     // -1 when invalid
QString numberedName(const QString& base, int number);

// connector types offered in the UI: value -> description
QList<QPair<int, QString> > connectorTypes();

}  // namespace UsbMap

#endif
