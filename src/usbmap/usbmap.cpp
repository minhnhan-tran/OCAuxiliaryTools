#include "usbmap.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace UsbMap {
namespace {

QString ltrim(const QString& s) {
  int i = 0;
  while (i < s.size() && s[i].isSpace()) ++i;
  return s.mid(i);
}
int indentOf(const QString& s) { return s.size() - ltrim(s).size(); }
QString noPipes(QString s) { return s.remove('|'); }

const QRegularExpression& rxPort() {
  static const QRegularExpression r("Apple[a-zA-Z0-9]*USB\\d*[A-Z]+Port,");
  return r;
}
const QRegularExpression& rxCont() {
  static const QRegularExpression r("Apple[a-zA-Z0-9]*USB[0-9A-Z]+,");
  return r;
}
const QRegularExpression& rxHub() {
  static const QRegularExpression r("Apple[a-zA-Z0-9]*USB\\d+[a-zA-Z]*Hub,");
  return r;
}
const QRegularExpression& rxHubPort() {
  static const QRegularExpression r("Apple[a-zA-Z0-9]*USB\\d+[a-zA-Z]*HubPort,");
  return r;
}
const QRegularExpression& rxBt() {
  static const QRegularExpression r("<class [a-zA-Z0-9]*BluetoothHostControllerUSBTransport,");
  return r;
}
const QRegularExpression& rxDev() {
  // IOUSBHostDevice classes that are not simply named IOUSBHostDevice (as USBMap)
  static const QRegularExpression r("^(?!.*IOUSBHostDevice@).*<class IOUSBHostDevice,");
  return r;
}
bool isDevice(const QString& l) { return rxBt().match(l).hasMatch() || rxDev().match(l).hasMatch(); }

struct Obj {
  QString line, id, name, type, fullName, address;
  int indent = 0;
};
bool objFromLine(const QString& line, Obj* o) {
  const int ci = line.indexOf("<class ");
  if (ci < 0) return false;
  o->line = line;
  o->indent = indentOf(line);
  o->id = line.section("id ", -1);
  o->fullName = ltrim(line).section("  <class", 0, 0);
  o->type = line.mid(ci + 7).section(',', 0, 0);
  o->address = o->fullName.section('@', -1);
  return true;
}

QString ioservicePath(const QStringList& ioreg, const QString& cleanedLine, const QString& smbios) {
  const QString lineId = "id " + cleanedLine.section(", registered", 0, 0).section("id ", -1);
  for (int index = 0; index < ioreg.size(); ++index) {
    if (!ioreg[index].contains(lineId)) continue;
    int indent = -1;
    QStringList path;
    for (int j = index; j >= 0; --j) {
      const QString& l = ioreg[j];
      if (!l.contains("  <class")) continue;
      QString chk = l;
      chk.replace('|', ' ').remove("+-o");
      const int ic = indentOf(chk);
      if (ic < indent || indent == -1) {
        const QString entry = ltrim(chk).section("  <class", 0, 0);
        if (entry == smbios) break;
        path << entry;
        indent = ic;
      }
    }
    QStringList rev;
    for (int k = path.size() - 1; k >= 0; --k) rev << path[k];
    return "IOService:/" + rev.join("/");
  }
  return QString();
}

QString xmlEsc(const QString& s) {
  QString r = s;
  r.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
  return r;
}

class Xml {
 public:
  void begin() {
    out_ = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\">\n";
    d_ = 0;
  }
  void end() { out_ += "</plist>\n"; }
  void open() { line("<dict>"); ++d_; }
  void close() { --d_; line("</dict>"); }
  void key(const QString& k) { line("<key>" + xmlEsc(k) + "</key>"); }
  void str(const QString& v) { line("<string>" + xmlEsc(v) + "</string>"); }
  void num(qint64 v) { line("<integer>" + QString::number(v) + "</integer>"); }
  void boolean(bool v) { line(v ? "<true/>" : "<false/>"); }
  void data(const QByteArray& b) { line("<data>" + QString::fromLatin1(b.toBase64()) + "</data>"); }
  void kv(const QString& k, const QString& v) { key(k); str(v); }
  QByteArray bytes() const { return out_.toUtf8(); }

 private:
  void line(const QString& s) { out_ += QString(d_, QChar('\t')) + s + "\n"; }
  QString out_;
  int d_ = 0;
};

QByteArray hexToData(const QString& v) { return QByteArray::fromHex(checkHex(v).toLatin1()); }

int findMatch(const ControllerList& into, const Controller& c) {
  // USBMap.get_matching_controller: locationid, pci_debug, ioservice_path, acpi_path
  for (int pass = 0; pass < 4; ++pass) {
    for (int i = 0; i < into.size(); ++i) {
      const QString a = pass == 0 ? c.locationId : pass == 1 ? c.pciDebug : pass == 2 ? c.ioservicePath : c.acpiPath;
      const QString b = pass == 0 ? into[i].locationId : pass == 1 ? into[i].pciDebug : pass == 2 ? into[i].ioservicePath : into[i].acpiPath;
      if (!a.isEmpty() && a == b) return i;
    }
  }
  return -1;
}

}  // namespace

QString checkHex(const QString& v) {
  QString s = v.toLower();
  s.replace("0x", "");
  s.remove(QRegularExpression("[^0-9a-f]+"));
  return s;
}

QString hexSwap(const QString& v) {
  QString h = checkHex(v);
  if (h.isEmpty()) return QString();
  if (h.size() % 2) h.prepend('0');
  QString r;
  for (int i = h.size() - 2; i >= 0; i -= 2) r += h.mid(i, 2);
  return r.toUpper();
}

qint64 hexDec(const QString& v) {
  bool ok = false;
  const qint64 n = checkHex(v).toLongLong(&ok, 16);
  return ok ? n : -1;
}

QString numberedName(const QString& base, int number) {
  const QString n = QString::number(number);
  return base.left(base.size() - n.size()) + n;
}

QList<QPair<int, QString> > connectorTypes() {
  QList<QPair<int, QString> > l;
  l << qMakePair(0, QString("USB 2 Type-A"))
    << qMakePair(3, QString("USB 3 Type-A"))
    << qMakePair(8, QString("Type-C, USB 2 only"))
    << qMakePair(9, QString("Type-C with switch"))
    << qMakePair(10, QString("Type-C without switch"))
    << qMakePair(255, QString("Internal"));
  return l;
}

QStringList sanitizeIoreg(const QString& text) {
  QString joined;
  const QStringList lines = text.split('\n');
  joined.reserve(text.size());
  for (const QString& line : lines)
    joined += line + ((line.contains("+-o") && !line.contains("  <class")) ? " " : "\n");
  return joined.split('\n');
}

bool discover(const QStringList& ioreg, ControllerList* out, QString* smbiosOut, QString* error,
              bool mapHubs) {
  out->clear();
  QString smbios;
  // lines USBMap looks at (cleaned the same way), with their index in `ioreg`
  struct V { QString line; int i; };
  QList<V> valid;
  for (int i = 0; i < ioreg.size(); ++i) {
    const QString& x = ioreg[i];
    const bool hit = rxCont().match(x).hasMatch() || rxPort().match(x).hasMatch() ||
                     rxHub().match(x).hasMatch() || isDevice(x) ||
                     (mapHubs && rxHubPort().match(x).hasMatch()) ||
                     x.contains("<class IOPlatformExpertDevice,") || x.contains("<class IOPCIDevice,");
    if (!hit) continue;
    QString c = x;
    c.replace('|', ' ');
    c.remove("+-o ");
    valid.append(V{c.section(", registered", 0, 0), i});
  }

  QMap<QString, int> idx;     // controller key -> position in *out
  QStringList contList;
  QString lastPciLine;
  int lastPciIndex = -1;
  int lastPortCont = -1;      // controller position + port key of the last port
  QString lastPortKey;

  for (int vi = 0; vi < valid.size(); ++vi) {
    const QString& line = valid[vi].line;
    const int i = valid[vi].i;
    if (line.contains("<class IOPlatformExpertDevice,")) {
      smbios = line.section("<class", 0, 0).trimmed();
      continue;
    }
    if (line.contains("<class IOPCIDevice,")) {
      lastPciLine = line;
      lastPciIndex = i;
      continue;
    }
    Obj o;
    if (!objFromLine(line, &o)) continue;
    // drop controllers that are not ancestors of this entry
    while (!contList.isEmpty() && (*out)[idx[contList.last()]].indent >= o.indent) contList.removeLast();

    if (rxCont().match(line).hasMatch()) {
      Controller c;
      c.key = o.fullName;
      c.name = o.fullName.section('@', 0, 0);
      c.address = o.address;
      c.type = o.type;
      c.indent = o.indent;
      if (idx.contains(c.key)) continue;  // duplicate key: keep the first
      if (lastPciIndex >= 0 && lastPciIndex + 1 < ioreg.size()) {
        const QString parent = lastPciLine.trimmed().section("  <class", 0, 0);
        c.parentName = parent.section('@', 0, 0);
        for (int j = lastPciIndex + 1; j < ioreg.size(); ++j) {
          const QString& l = ioreg[j];
          if (noPipes(l).trimmed() == "}") break;
          if (l.contains("\"acpi-path\"")) c.acpiPath = l.section('"', -2, -2);
          else if (l.contains("\"pcidebug\"")) c.pciDebug = l.section('"', -2, -2);
          if (!c.acpiPath.isEmpty() && !c.pciDebug.isEmpty()) break;
        }
      }
      out->append(c);
      idx[c.key] = out->size() - 1;
      contList.clear();
      contList << c.key;
      lastPortCont = -1;
      lastPortKey.clear();
    } else if (contList.isEmpty()) {
      continue;
    } else if (mapHubs && rxHub().match(line).hasMatch() &&
               (*out)[idx[contList.first()]].type.contains("EHCI")) {
      Controller h;
      h.key = "HUB-" + o.address;
      h.name = o.fullName.section('@', 0, 0);
      h.address = o.address;
      h.type = o.type;
      h.indent = o.indent;
      h.isHub = true;
      h.locationId = QString::number(hexDec(o.address));
      h.parentName = (*out)[idx[contList.first()]].name;
      out->append(h);
      idx[h.key] = out->size() - 1;
      if (lastPortCont >= 0 && (*out)[lastPortCont].ports.contains(lastPortKey))
        (*out)[lastPortCont].ports[lastPortKey].containsHub = true;
      contList << h.key;
    } else if (rxPort().match(line).hasMatch() || (mapHubs && rxHubPort().match(line).hasMatch())) {
      if (rxHubPort().match(line).hasMatch() &&
          !(*out)[idx[contList.first()]].type.contains("EHCI"))
        continue;
      if (i + 1 >= ioreg.size()) continue;
      Port p;
      p.name = o.fullName.section('@', 0, 0);
      p.type = o.type;
      p.address = o.address;
      for (int j = i + 1; j < ioreg.size(); ++j) {
        const QString& l = ioreg[j];
        if (noPipes(l).trimmed() == "}") break;
        if (l.contains("\"port\" = ") || l.contains("\"usb-port-number\" = ")) {
          p.key = l.section('<', 1, 1).section('>', 0, 0);
        } else if (l.contains("\"UsbConnector\" = ") || l.contains("\"usb-port-type\" = ")) {
          bool ok = false;
          const int v = l.section(" = ", 1, 1).trimmed().toInt(&ok);
          p.connector = ok ? v : -1;
        }
      }
      if (p.key.isEmpty()) continue;
      // devices on this port: later entries that sit deeper than the port
      for (int k = vi + 1; k < valid.size(); ++k) {
        if (indentOf(valid[k].line) <= o.indent) break;
        if (isDevice(valid[k].line)) {
          const QString dn = ltrim(valid[k].line).section("  <class", 0, 0).section('@', 0, 0);
          if (!dn.isEmpty() && !p.items.contains(dn)) p.items << dn;
        }
      }
      const int ci = idx[contList.last()];
      (*out)[ci].ports[p.key] = p;
      lastPortCont = ci;
      lastPortKey = p.key;
    }
  }
  for (int i = 0; i < out->size(); ++i) {
    if ((*out)[i].isHub) continue;
    // cleaned line of the controller, as USBMap passes it
    for (int vi = 0; vi < valid.size(); ++vi) {
      Obj o;
      if (objFromLine(valid[vi].line, &o) && o.fullName == (*out)[i].key && rxCont().match(valid[vi].line).hasMatch()) {
        const QString path = ioservicePath(ioreg, valid[vi].line, smbios);
        if (!path.isEmpty()) (*out)[i].ioservicePath = path;
        break;
      }
    }
  }
  *smbiosOut = smbios;
  bool any = false;
  for (int i = 0; i < out->size(); ++i) if (!(*out)[i].isHub) any = true;
  if (!any) {
    if (error) *error = "No USB controllers were found in the ioreg output.";
    return false;
  }
  return true;
}

void merge(ControllerList* into, const ControllerList& from) {
  for (int f = 0; f < from.size(); ++f) {
    const Controller& src = from[f];
    int pos = findMatch(*into, src);
    if (pos < 0) {
      for (int i = 0; i < into->size(); ++i) if ((*into)[i].key == src.key) { pos = i; break; }
    }
    if (pos < 0) {
      Controller c = src;
      into->append(c);
      continue;
    }
    Controller& dst = (*into)[pos];
    const QMap<QString, Port> keep = dst.ports;
    const QString keepKey = dst.key;
    dst = src;           // controller properties come from the fresh discovery
    dst.key = keepKey;   // but the saved entry keeps its identity
    dst.ports = keep;
    for (QMap<QString, Port>::const_iterator it = src.ports.begin(); it != src.ports.end(); ++it) {
      const Port& np = it.value();
      if (!dst.ports.contains(it.key())) { dst.ports[it.key()] = np; continue; }
      Port& op = dst.ports[it.key()];
      op.type = np.type;
      op.address = np.address;
      op.connector = np.connector;
      if (np.containsHub) op.containsHub = true;
      for (const QString& d : np.items) if (!op.items.contains(d)) op.items << d;
    }
  }
}

int enabledCount(const Controller& c) {
  int n = 0;
  for (QMap<QString, Port>::const_iterator it = c.ports.begin(); it != c.ports.end(); ++it)
    if (it.value().enabled == 1) ++n;
  return n;
}

QByteArray infoPlist(const ControllerList& list, const QString& smbios, bool legacy, QString* error) {
  if (smbios.isEmpty()) {
    if (error) *error = "The SMBIOS model is unknown; discover the ports first.";
    return QByteArray();
  }
  Xml x;
  x.begin();
  x.open();
  x.kv("CFBundleDevelopmentRegion", "English");
  x.kv("CFBundleGetInfoString", "v1.0");
  x.kv("CFBundleIdentifier", "com.corpnewt.USBMap");
  x.kv("CFBundleInfoDictionaryVersion", "6.0");
  x.kv("CFBundleName", "USBMap");
  x.kv("CFBundlePackageType", "KEXT");
  x.kv("CFBundleShortVersionString", "1.0");
  x.kv("CFBundleSignature", "????");
  x.kv("CFBundleVersion", "1.0");
  x.key("IOKitPersonalities");
  x.open();
  QStringList used;
  int built = 0;
  for (int ci = 0; ci < list.size(); ++ci) {
    const Controller& c = list[ci];
    if (enabledCount(c) == 0) continue;  // nothing enabled: skipped, as USBMap does
    const bool xhci = c.type.contains("XHCI");
    enum { NameMatch, PathMatch, ParentMatch, Location } save = NameMatch;
    if (!c.locationId.isEmpty()) save = Location;
    else if (!c.pciDebug.isEmpty()) save = ParentMatch;
    else if (!c.ioservicePath.isEmpty()) save = PathMatch;

    QString entry = smbios + "-" + c.key.section('@', 0, 0);
    for (int n = 0;; ++n) {
      const QString t = n ? entry + "-" + QString::number(n) : entry;
      if (!used.contains(t)) { entry = t; break; }
    }
    used << entry;

    // pass 1: the highest enabled port number becomes port-count
    int top = 0;
    QByteArray topData = QByteArray::fromHex("00000000");
    for (QMap<QString, Port>::const_iterator it = c.ports.begin(); it != c.ports.end(); ++it) {
      const qint64 num = hexDec(hexSwap(it.value().key));
      if (it.value().enabled == 1 && num > top) { top = int(num); topData = hexToData(it.value().key); }
    }

    x.key(entry);
    x.open();
    x.kv("CFBundleIdentifier", legacy ? "com.apple.driver.AppleUSBMergeNub" : "com.apple.driver.AppleUSBHostMergeProperties");
    x.kv("IOClass", legacy ? "AppleUSBMergeNub" : "AppleUSBHostMergeProperties");
    if (save == NameMatch) x.kv("IONameMatch", c.parentName);
    if (save == PathMatch) x.kv("IOPathMatch", c.ioservicePath);
    if (save == ParentMatch) {
      x.key("IOParentMatch");
      x.open();
      x.key("IOPropertyMatch");
      x.open();
      x.kv("pcidebug", c.pciDebug);
      x.close();
      x.close();
    }
    x.kv("IOProviderClass", save == Location ? QString("AppleUSB20InternalHub") : c.type);
    x.key("IOProviderMergeProperties");
    x.open();
    x.key("kUSBMuxEnabled");
    x.boolean(xhci);
    x.key("port-count");
    x.data(topData);
    x.key("ports");
    x.open();
    // pass 2: every port, numbered by kind (disabled ones are kept but commented out with '#')
    int hs = 0, ss = 0, uk = 0;
    for (QMap<QString, Port>::const_iterator it = c.ports.begin(); it != c.ports.end(); ++it) {
      const Port& p = it.value();
      QString name;
      if (p.type == "Unknown") name = numberedName("UK00", ++uk);
      else if (p.type.contains("USB3")) name = numberedName("SS00", ++ss);
      else name = numberedName(xhci ? "HS00" : "PRT0", ++hs);
      int conn = p.typeOverride >= 0 ? p.typeOverride : (p.containsHub ? 255 : p.connector);
      if (conn == -1) conn = xhci ? 3 : 0;
      const QString pre = p.enabled == 1 ? QString() : QString("#");
      x.key(name);
      x.open();
      x.key("UsbConnector");
      x.num(conn);
      x.key(pre + "port");
      x.data(hexToData(p.key));
      x.key("usb-port-type");
      x.num(conn);
      x.key(pre + "usb-port-number");
      x.data(hexToData(p.key));
      if (!p.comment.isEmpty()) x.kv("Comment", p.comment);
      x.close();
    }
    x.close();  // ports
    x.close();  // IOProviderMergeProperties
    x.kv("model", smbios);
    if (save == Location) {
      x.key("locationID");
      x.num(c.locationId.toLongLong());
      x.key("IOProbeScore");
      x.num(5000);
    }
    x.close();  // personality
    ++built;
  }
  x.close();  // IOKitPersonalities
  x.kv("OSBundleRequired", "Root");
  x.close();
  x.end();
  if (!built) {
    if (error) *error = "No ports are enabled. Enable at least one port first.";
    return QByteArray();
  }
  return x.bytes();
}

QString writeKext(const QString& dir, const ControllerList& list, const QString& smbios,
                  bool legacy, QString* error) {
  const QByteArray plist = infoPlist(list, smbios, legacy, error);
  if (plist.isEmpty()) return QString();
  const QString kext = QDir(dir).filePath(legacy ? "USBMapLegacy.kext" : "USBMap.kext");
  const QString contents = kext + "/Contents";
  if (!QDir().mkpath(contents)) {
    if (error) *error = "Cannot create " + contents;
    return QString();
  }
  QSaveFile f(contents + "/Info.plist");
  if (!f.open(QIODevice::WriteOnly) || f.write(plist) != plist.size() || !f.commit()) {
    if (error) *error = "Cannot write " + contents + "/Info.plist";
    return QString();
  }
  return kext;
}

QByteArray toJson(const ControllerList& list, const QString& smbios) {
  QJsonArray cs;
  for (int i = 0; i < list.size(); ++i) {
    const Controller& c = list[i];
    QJsonObject o;
    o["key"] = c.key; o["name"] = c.name; o["type"] = c.type; o["address"] = c.address;
    o["parentName"] = c.parentName; o["pciDebug"] = c.pciDebug; o["acpiPath"] = c.acpiPath;
    o["ioservicePath"] = c.ioservicePath; o["isHub"] = c.isHub; o["locationId"] = c.locationId;
    QJsonArray ps;
    for (QMap<QString, Port>::const_iterator it = c.ports.begin(); it != c.ports.end(); ++it) {
      const Port& p = it.value();
      QJsonObject q;
      q["key"] = p.key; q["name"] = p.name; q["type"] = p.type; q["address"] = p.address;
      q["connector"] = p.connector; q["enabled"] = p.enabled; q["typeOverride"] = p.typeOverride;
      q["comment"] = p.comment; q["containsHub"] = p.containsHub;
      q["items"] = QJsonArray::fromStringList(p.items);
      ps.append(q);
    }
    o["ports"] = ps;
    cs.append(o);
  }
  QJsonObject root;
  root["version"] = 1;
  root["smbios"] = smbios;
  root["controllers"] = cs;
  return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool fromJson(const QByteArray& data, ControllerList* list, QString* smbios) {
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) return false;
  const QJsonObject root = doc.object();
  list->clear();
  *smbios = root["smbios"].toString();
  const QJsonArray cs = root["controllers"].toArray();
  for (int i = 0; i < cs.size(); ++i) {
    const QJsonObject o = cs[i].toObject();
    Controller c;
    c.key = o["key"].toString(); c.name = o["name"].toString(); c.type = o["type"].toString();
    c.address = o["address"].toString(); c.parentName = o["parentName"].toString();
    c.pciDebug = o["pciDebug"].toString(); c.acpiPath = o["acpiPath"].toString();
    c.ioservicePath = o["ioservicePath"].toString(); c.isHub = o["isHub"].toBool();
    c.locationId = o["locationId"].toString();
    if (c.key.isEmpty()) continue;
    const QJsonArray ps = o["ports"].toArray();
    for (int k = 0; k < ps.size(); ++k) {
      const QJsonObject q = ps[k].toObject();
      Port p;
      p.key = q["key"].toString(); p.name = q["name"].toString(); p.type = q["type"].toString();
      p.address = q["address"].toString(); p.connector = q["connector"].toInt(-1);
      p.enabled = q["enabled"].toInt(-1); p.typeOverride = q["typeOverride"].toInt(-1);
      p.comment = q["comment"].toString(); p.containsHub = q["containsHub"].toBool();
      const QJsonArray it = q["items"].toArray();
      for (int m = 0; m < it.size(); ++m) p.items << it[m].toString();
      if (!p.key.isEmpty()) c.ports[p.key] = p;
    }
    list->append(c);
  }
  return true;
}

}  // namespace UsbMap
