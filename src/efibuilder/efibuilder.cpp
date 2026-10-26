// See efibuilder.h. Ported from lzhoang2801/OpCore-Simplify (BSD-3-Clause).
#include "efibuilder.h"

#include <QDir>
#include <QFile>
#include <QtEndian>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace EfiBuilder {

namespace {

const QJsonObject& data() {
  static QJsonObject d = [] {
    QFile f(":/efibuilder/efidata.json");
    // Standalone tests may point at a checkout of the JSON instead.
    if (!f.open(QIODevice::ReadOnly)) {
      f.setFileName(qEnvironmentVariable("EFIDATA_PATH"));
      f.open(QIODevice::ReadOnly);
    }
    return QJsonDocument::fromJson(f.readAll()).object();
  }();
  return d;
}

QStringList pciList(const QString& key) {
  QStringList out;
  for (const QJsonValue& v : data()["pci_data"].toObject()[key].toArray())
    out << v.toString();
  return out;
}

bool inPci(const QString& key, const QString& id) {
  return pciList(key).contains(id);
}

QJsonArray osVersions() { return data()["os_data"].toObject()["macos_versions"].toArray(); }

int latestDarwin(bool includeBeta = true) {
  int m = 0;
  for (const QJsonValue& v : osVersions()) {
    QJsonObject o = v.toObject();
    if (!includeBeta && o["release_status"].toString() != "final") continue;
    m = qMax(m, o["darwin_version"].toInt());
  }
  return m;
}

int lowestDarwin() {
  int m = 1 << 20;
  for (const QJsonValue& v : osVersions()) m = qMin(m, v.toObject()["darwin_version"].toInt());
  return m;
}

QString latestV(bool beta = true) { return QString("%1.99.99").arg(latestDarwin(beta)); }
QString lowestV() { return QString("%1.0.0").arg(lowestDarwin()); }

qint64 vkey(const QString& v) {
  QStringList p = v.split('.');
  auto at = [&](int i) { return i < p.size() ? p[i].toLongLong() : 0; };
  return at(0) * 1000000 + at(1) * 1000 + at(2);
}

bool containsAny(const QStringList& items, const QString& text) {
  for (const QString& i : items)
    if (text.contains(i)) return true;
  return false;
}

QStringList jsonStrings(const QJsonValue& v) {
  QStringList out;
  for (const QJsonValue& x : v.toArray()) out << x.toString();
  return out;
}

// Widest range across devices that are supported at all.
Range widest(const QList<Range>& rs) {
  Range w;
  for (const Range& r : rs) {
    if (!r.supported()) continue;
    if (w.maxV.isEmpty() || darwinLess(w.maxV, r.maxV)) w.maxV = r.maxV;
    if (w.minV.isEmpty() || darwinLess(r.minV, w.minV)) w.minV = r.minV;
  }
  return w;
}

void restrictNative(Compatibility* c, const Range& r) {
  if (!r.supported()) return;
  if (c->nativeMax.isEmpty() || darwinLess(r.maxV, c->nativeMax)) c->nativeMax = r.maxV;
  if (c->nativeMin.isEmpty() || darwinLess(c->nativeMin, r.minV)) c->nativeMin = r.minV;
}

QString codename(const QJsonObject& cpu) { return cpu["Codename"].toString(); }

}  // namespace

int darwinMajor(const QString& v) { return v.section('.', 0, 0).toInt(); }

bool darwinLess(const QString& a, const QString& b) { return vkey(a) < vkey(b); }

QString macosName(int darwinMajor) {
  for (const QJsonValue& v : osVersions()) {
    QJsonObject o = v.toObject();
    if (o["darwin_version"].toInt() == darwinMajor)
      return QString("macOS %1 (%2)").arg(o["name"].toString(), o["macos_version"].toString());
  }
  return QString("Darwin %1").arg(darwinMajor);
}

// ---------------------------------------------------------------- report ----

QStringList validateReport(const QJsonObject& r) {
  QStringList errs;
  for (const char* k : {"Motherboard", "CPU", "GPU"})
    if (!r.contains(k) || !r[k].isObject() || r[k].toObject().isEmpty())
      errs << QString("Missing or empty section: %1").arg(k);
  if (!errs.isEmpty()) return errs;

  const QJsonObject cpu = r["CPU"].toObject();
  static const QRegularExpression devId("^[0-9A-F]{4}(-[0-9A-F]{4})?$");
  if (!QRegularExpression("^(Intel|AMD)$").match(cpu["Manufacturer"].toString()).hasMatch())
    errs << "CPU.Manufacturer must be Intel or AMD";
  if (cpu["Processor Name"].toString().isEmpty()) errs << "CPU.Processor Name is empty";
  if (!cpu["SIMD Features"].isString()) errs << "CPU.SIMD Features is missing";
  if (!QRegularExpression("^(Desktop|Laptop)$")
           .match(r["Motherboard"].toObject()["Platform"].toString()).hasMatch())
    errs << "Motherboard.Platform must be Desktop or Laptop";

  const QJsonObject gpus = r["GPU"].toObject();
  for (auto it = gpus.begin(); it != gpus.end(); ++it) {
    QJsonObject g = it.value().toObject();
    if (!QRegularExpression("^(Intel|AMD|NVIDIA)$").match(g["Manufacturer"].toString()).hasMatch())
      errs << QString("GPU \"%1\": Manufacturer must be Intel, AMD or NVIDIA").arg(it.key());
    if (!devId.match(g["Device ID"].toString()).hasMatch())
      errs << QString("GPU \"%1\": invalid Device ID").arg(it.key());
  }
  for (const char* sec : {"Network", "Sound", "Bluetooth", "Storage Controllers"}) {
    const QJsonObject o = r[sec].toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
      if (!devId.match(it.value().toObject()["Device ID"].toString()).hasMatch())
        errs << QString("%1 \"%2\": invalid Device ID").arg(sec, it.key());
  }
  return errs;
}

bool loadReport(const QString& path, QJsonObject* report, QStringList* errors) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    *errors << QString("Cannot open %1").arg(path);
    return false;
  }
  QJsonParseError pe;
  QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    *errors << QString("Not a valid JSON report: %1").arg(pe.errorString());
    return false;
  }
  *errors = validateReport(doc.object());
  if (!errors->isEmpty()) return false;
  *report = doc.object();
  return true;
}

// ---------------------------------------------------------- compatibility ----

Compatibility checkCompatibility(const QJsonObject& r) {
  Compatibility c;
  c.nativeMax = latestV();
  c.nativeMin = lowestV();
  const QJsonObject cpu = r["CPU"].toObject();
  const QString simd = cpu["SIMD Features"].toString();
  const QString platform = r["Motherboard"].toObject()["Platform"].toString();
  const QJsonObject monitors = r["Monitor"].toObject();

  // CPU ---------------------------------------------------------------
  Range cr{latestV(), lowestV()};
  if (!simd.contains("SSE4")) {
    cr = Range();
  } else if (!simd.contains("SSE4.2")) {
    cr.minV = "18.0.0";
    if (simd.contains("SSE4.1")) cr.maxV = "21.99.99";
  }
  c.devices.append({"CPU", cpu["Processor Name"].toString(), cr, codename(cpu)});
  if (!cr.supported()) {
    c.cpuSupported = false;
    c.nativeMax.clear();
    c.nativeMin.clear();
    c.warnings << "CPU lacks SSE4.x and cannot run any supported macOS.";
    return c;
  }
  restrictNative(&c, cr);

  // GPU ---------------------------------------------------------------
  const bool lowEnd = cpu["Processor Name"].toString().contains("Celeron") ||
                      cpu["Processor Name"].toString().contains("Pentium");
  const QStringList amdCodenames = jsonStrings(data()["gpu_data"].toObject()["AMDCodenames"]);
  const QJsonObject gpus = r["GPU"].toObject();
  QMap<QString, Range> gpuRange;
  for (auto it = gpus.begin(); it != gpus.end(); ++it) {
    const QJsonObject g = it.value().toObject();
    const QString mfr = g["Manufacturer"].toString(), cn = g["Codename"].toString();
    const QString id = g["Device ID"].toString().mid(5);
    Range rg{latestV(), lowestV()};
    QString note;
    auto startsAny = [&](std::initializer_list<const char*> p) {
      for (const char* s : p) if (id.startsWith(s)) return true;
      return false;
    };
    auto avx2 = [&] { return simd.contains("AVX2"); };
    if (mfr.contains("Intel")) {
      if (startsAny({"0042", "0046"}) && platform != "Desktop") rg.maxV = "17.99.99";
      else if (id.startsWith("01") && !QString("56").contains(id.at(id.size() - 2)) &&
               !QStringList{"0102", "0106", "010A"}.contains(id)) rg.maxV = "17.99.99";
      else if (id.startsWith("01") && !QStringList{"0152", "0156"}.contains(id)) rg.maxV = "20.99.99";
      else if (startsAny({"04", "0A", "0C", "0D", "0B", "16"})) rg.maxV = "21.99.99";
      else if (startsAny({"09", "19", "59", "3E", "87", "9B"}) &&
               !QStringList{"3E90", "3E93", "3E99", "3E9C", "3EA1", "3EA4", "9B21", "9BA0", "9BA2",
                            "9BA4", "9BA5", "9BA8", "9BAA", "9BAB", "9BAC"}.contains(id)) {}
      else if (id.startsWith("8A")) rg.minV = "19.4.0";
      else rg = Range();
      if (lowEnd) rg = Range();
    } else if (mfr.contains("AMD")) {
      if (cn.contains("Navi 2")) {
        if (!avx2()) { rg.maxV = "21.99.99"; note = "needs OCLP-style root patches on 13+ (no AVX2)"; }
        else if (cn == "Navi 23" || cn == "Navi 22") rg.minV = "21.2.0";
        else if (cn.contains("Navi 21")) rg.minV = "20.5.0";
        else rg = Range();
      } else if (cn.contains("Navi 1")) {
        if (!avx2()) { rg.maxV = "21.99.99"; note = "no AVX2: limited to macOS 12"; }
        rg.minV = "19.0.0";
      } else if (cn.contains("Vega 20")) {
        if (!avx2()) rg.maxV = "21.99.99";
        rg.minV = "18.6.0";
      } else if (QStringList{"Vega 10", "Polaris 22", "Polaris 20", "Baffin", "Ellesmere"}.contains(cn) ||
                 id == "6995" || id == "699F") {
        if (!avx2()) rg.maxV = "21.99.99";
        rg.minV = "17.0.0";
      } else if (containsAny(amdCodenames, cn)) rg.maxV = "21.99.99";
      else if (QStringList{"15D8", "15DD", "15E7", "1636", "1638", "164C"}.contains(id)) rg.minV = "19.0.0";
      else rg = Range();
    } else if (mfr.contains("NVIDIA")) {
      if (cn.contains("Kepler")) rg.maxV = "20.99.99";
      else if (QStringList{"Pascal", "Maxwell", "Fermi", "Tesla"}.contains(cn)) { rg.maxV = "17.99.99"; rg.minV = "17.0.0"; }
      else rg = Range();
    }
    // Intel gen 2-4 iGPUs only drive VGA here -> unusable (original `all()`
    // is vacuously true with no monitors; we require at least one).
    if (mfr.contains("Intel") && startsAny({"01", "04", "0A", "0C", "0D"}) && !monitors.isEmpty()) {
      bool allVga = true;
      for (auto m = monitors.begin(); m != monitors.end(); ++m) {
        QJsonObject mo = m.value().toObject();
        if (!(mo["Connector Type"].toString() == "VGA" &&
              mo.value("Connected GPU").toString(it.key()) == it.key())) { allVga = false; break; }
      }
      if (allVga) rg = Range();
    }
    gpuRange[it.key()] = rg;
    c.devices.append({"GPU", it.key(), rg, note});
  }
  // A display wired to a different GPU than an "Internal" panel invalidates
  // the other adapter.
  for (auto it = gpus.begin(); it != gpus.end(); ++it) {
    Range& rg = gpuRange[it.key()];
    if (!rg.supported()) continue;
    bool othersAllNone = true;
    for (auto o = gpuRange.begin(); o != gpuRange.end(); ++o)
      if (o.key() != it.key() && o.value().supported()) othersAllNone = false;
    if (othersAllNone) continue;
    for (auto m = monitors.begin(); m != monitors.end(); ++m) {
      QJsonObject mo = m.value().toObject();
      if (mo["Connector Type"].toString() == "Internal" &&
          mo.value("Connected GPU").toString(it.key()) != it.key()) {
        rg = Range();
        for (auto& d : c.devices) if (d.kind == "GPU" && d.name == it.key()) d.range = rg;
        break;
      }
    }
  }
  const Range gw = widest(gpuRange.values());
  if (!gw.supported()) c.warnings << "No GPU is natively supported; expect to need OCLP/root patches or a supported dGPU.";
  else restrictNative(&c, gw);

  // Sound -------------------------------------------------------------
  const QJsonObject layouts = data()["codec_layouts"].toObject()["data"].toObject();
  const QJsonObject sound = r["Sound"].toObject();
  const QStringList sst = pciList("IntelSSTIDs");
  for (auto it = sound.begin(); it != sound.end(); ++it) {
    const QJsonObject s = it.value().toObject();
    const QString id = s["Device ID"].toString();
    Range rg;
    if (s["Bus Type"].toString().contains("USB") || id.startsWith("1002") ||
        (id.startsWith("8086") && !sst.contains(id)) || layouts.contains(id))
      rg = Range{latestV(), lowestV()};
    c.devices.append({"Sound", it.key(), rg, rg.supported() ? QString() : "no AppleALC layout"});
  }

  // Biometric ---------------------------------------------------------
  const QJsonObject bio = r["Biometric"].toObject();
  for (auto it = bio.begin(); it != bio.end(); ++it)
    c.devices.append({"Biometric", it.key(), Range(), "unsupported"});

  // Network -----------------------------------------------------------
  const QStringList bcm = pciList("BroadcomWiFiIDs"), ath = pciList("AtherosWiFiIDs");
  const QStringList eth = pciList("EthernetIDs"), wusb = pciList("WirelessUSBIDs"),
                    wcard = pciList("WirelessCardIDs"), intelWifi = pciList("IntelWiFiIDs"),
                    rtw = pciList("rtw88WiFiIDs");
  const QJsonObject net = r["Network"].toObject();
  for (auto it = net.begin(); it != net.end(); ++it) {
    const QJsonObject n = it.value().toObject();
    const QString id = n["Device ID"].toString();
    Range rg{latestV(), lowestV()};
    QString note;
    if (bcm.contains(id)) {
      int idx = bcm.indexOf(id);
      if (idx == 13 || (idx > 17 && idx < 21)) rg.maxV = "22.99.99";
      else if (idx < 12) rg.maxV = "17.99.99";
    } else if (ath.mid(0, 8).contains(id)) rg.maxV = "17.99.99";
    else if (ath.mid(8).contains(id)) rg.maxV = "20.99.99";
    else if (pciList("IntelI22XIDs").contains(id)) rg.minV = "19.0.0";
    else if (pciList("AquantiaAqtionIDs").contains(id)) rg.minV = "21.0.0";
    const bool known = eth.contains(id) || wusb.contains(id) || wcard.contains(id);
    if (!known) rg = Range();
    if (wcard.contains(id) && !intelWifi.contains(id) && !ath.mid(8).contains(id) && !rtw.contains(id))
      note = "Wi-Fi works only with OCLP root patches on newer macOS";
    if (intelWifi.contains(id)) note = "Intel Wi-Fi: use AirportItlwm/itlwm";
    c.devices.append({"Network", it.key(), rg, note});
  }

  // Storage -----------------------------------------------------------
  const QJsonObject sto = r["Storage Controllers"].toObject();
  QList<Range> stoRanges;
  const QStringList vmd = pciList("IntelVMDIDs");
  const QJsonObject unsupportedNvme = data()["pci_data"].toObject()["UnsupportedNVMeSSDIDs"].toObject();
  for (auto it = sto.begin(); it != sto.end(); ++it) {
    const QJsonObject s = it.value().toObject();
    if (s["Bus Type"].toString() != "PCI" || s["Disk Drives"].toArray().isEmpty()) continue;
    const QString id = s["Device ID"].toString();
    const QString sub = s.value("Subsystem ID").toString("00000000");
    Range rg{latestV(), lowestV()};
    QString note;
    if (vmd.contains(id)) { rg = Range(); note = "Intel VMD: disable in BIOS"; }
    else if (jsonStrings(unsupportedNvme[id]).contains(sub)) { rg = Range(); note = "known-bad NVMe SSD"; }
    else if (id == "1C5C-174A") rg.minV = "20.0.0";
    stoRanges << rg;
    c.devices.append({"Storage", it.key(), rg, note});
  }
  if (!stoRanges.isEmpty()) {
    const Range sw = widest(stoRanges);
    if (!sw.supported()) c.warnings << "No supported storage controller found (NVMe/SATA); macOS may not install.";
    else restrictNative(&c, sw);
  }

  // Bluetooth ---------------------------------------------------------
  const QJsonObject bt = r["Bluetooth"].toObject();
  const QStringList athBt = pciList("AtherosBluetoothIDs"), allBt = pciList("BluetoothIDs");
  for (auto it = bt.begin(); it != bt.end(); ++it) {
    const QString id = it.value().toObject()["Device ID"].toString();
    Range rg{latestV(), lowestV()};
    if (athBt.contains(id)) rg.maxV = "20.99.99";
    else if (!allBt.contains(id)) rg = Range();
    c.devices.append({"Bluetooth", it.key(), rg, QString()});
  }

  if (darwinLess(c.nativeMax, c.nativeMin) && !c.nativeMax.isEmpty())
    c.warnings << "Hardware constraints conflict: no macOS version satisfies all required devices.";
  return c;
}

QList<QPair<QString, int>> macosChoices(const Compatibility& c) {
  QList<QPair<QString, int>> out;
  if (c.nativeMax.isEmpty()) return out;
  const int hi = darwinMajor(c.nativeMax), lo = darwinMajor(c.nativeMin);
  for (const QJsonValue& v : osVersions()) {
    const int d = v.toObject()["darwin_version"].toInt();
    if (d >= lo && d <= hi) out.append({macosName(d), d});
  }
  std::sort(out.begin(), out.end(),
            [](const QPair<QString, int>& a, const QPair<QString, int>& b) { return a.second > b.second; });
  return out;
}

// ------------------------------------------------------- customization ----

QList<SmbiosModel> smbiosModels(const QJsonObject& r, const QString& target) {
  const bool laptop = r["Motherboard"].toObject()["Platform"].toString() == "Laptop";
  QList<SmbiosModel> out;
  for (const QJsonValue& v : data()["mac_model_data"].toObject()["mac_devices"].toArray()) {
    const QJsonObject o = v.toObject();
    SmbiosModel m;
    m.name = o["name"].toString();
    m.cpu = o["cpu"].toString();
    m.gpu = o["discrete_gpu"].toString();
    m.minV = o["initial_support"].toString();
    m.maxV = o["last_supported_version"].toString();
    m.supported = !target.isEmpty() && !darwinLess(target, m.minV) && !darwinLess(m.maxV, target);
    m.platformMatch = laptop == m.name.startsWith("MacBook");
    out.append(m);
  }
  return out;
}

QJsonObject kextRecord(const QString& name) {
  for (const QJsonValue& v : data()["kext_data"].toObject()["kexts"].toArray())
    if (v.toObject()["name"].toString() == name) return v.toObject();
  return QJsonObject();
}

QList<KextInfo> kextCatalog(const QString& target, const QStringList& database) {
  QList<KextInfo> out;
  for (const QJsonValue& v : data()["kext_data"].toObject()["kexts"].toArray()) {
    const QJsonObject o = v.toObject();
    KextInfo k;
    k.name = o["name"].toString();
    k.category = o["category"].toString();
    k.description = o["description"].toString();
    k.minV = o["min_darwin_version"].toString();
    k.maxV = o["max_darwin_version"].toString();
    k.required = o["required"].toBool();
    k.conflictGroup = o["conflict_group_id"].isString() ? o["conflict_group_id"].toString() : QString();
    if (k.conflictGroup == "None") k.conflictGroup.clear();
    k.requires_ = jsonStrings(o["requires_kexts"]);
    k.compatible = target.isEmpty() || (!darwinLess(target, k.minV) && !darwinLess(k.maxV, target));
    k.inDatabase = database.contains(k.name);
    out.append(k);
  }
  return out;
}

QStringList resolveKexts(const QStringList& wanted, const QString& target) {
  const QList<KextInfo> cat = kextCatalog(target, QStringList());
  QMap<QString, KextInfo> byName;
  for (const KextInfo& k : cat) byName[k.name] = k;
  QStringList out;
  QSet<QString> groups;
  for (const QString& n : wanted) {
    if (!byName.contains(n)) { out << n; continue; }  // unknown: keep, user's call
    const KextInfo& k = byName[n];
    if (!k.compatible) continue;
    if (!k.conflictGroup.isEmpty()) {
      if (groups.contains(k.conflictGroup)) continue;
      groups.insert(k.conflictGroup);
    }
    out << n;
  }
  // Pull in dependencies (e.g. Lilu for WhateverGreen) that are compatible.
  for (int i = 0; i < out.size(); ++i) {
    if (!byName.contains(out[i])) continue;
    for (const QString& dep : byName[out[i]].requires_)
      if (!out.contains(dep) && byName.contains(dep) && byName[dep].compatible) out << dep;
  }
  out.removeDuplicates();
  return out;
}

void overrideSmbios(Plan* p, const QString& model) {
  if (model.isEmpty() || model == p->smbios) return;
  p->notes << QString("SMBIOS overridden to %1 (auto-pick was %2); the base config may not match.")
                  .arg(model, p->smbios);
  p->smbios = model;
}

void overrideKexts(Plan* p, const QStringList& kexts) {
  p->kexts = kexts;
  std::sort(p->kexts.begin(), p->kexts.end());
}

// ------------------------------------------------------------- post-install ----

namespace {
const QJsonObject& postData() {
  static QJsonObject d = [] {
    QFile f(":/efibuilder/postinstall.json");
    if (!f.open(QIODevice::ReadOnly)) {
      f.setFileName(qEnvironmentVariable("POSTINSTALL_PATH"));
      f.open(QIODevice::ReadOnly);
    }
    return QJsonDocument::fromJson(f.readAll()).object();
  }();
  return d;
}
}  // namespace

QString postInstallGuideUrl() {
  const QString u = postData()["guide"].toString();
  return u.isEmpty() ? QString("https://dortania.github.io/OpenCore-Post-Install/") : u;
}

QList<NextStep> nextSteps(const QJsonObject& r, const Plan& p) {
  const QJsonObject mb = r["Motherboard"].toObject();
  const QString vendor = r["CPU"].toObject()["Manufacturer"].toString();
  auto holds = [&](const QString& cond) {
    if (cond == "laptop") return mb["Platform"].toString().contains("Laptop");
    if (cond == "desktop") return !mb["Platform"].toString().contains("Laptop");
    if (cond == "intel") return vendor.contains("Intel");
    if (cond == "amd") return vendor.contains("AMD");
    if (cond == "audio") return p.audioLayout > 0;
    if (cond.startsWith("kext:")) return p.kexts.contains(cond.mid(5));
    return false;  // unknown condition: hide the step rather than show a wrong one
  };
  QList<NextStep> out;
  for (const QJsonValue& v : postData()["steps"].toArray()) {
    const QJsonObject o = v.toObject();
    bool ok = true;
    for (const QJsonValue& c : o["when"].toArray()) ok = ok && holds(c.toString());
    if (!ok) continue;
    NextStep s;
    s.title = o["title"].toString();
    s.text = o["text"].toString();
    s.url = o["url"].toString();
    if (!s.title.isEmpty()) out << s;
  }
  return out;
}

void finalizeConfig(QVariantMap* config, const Finalize& f, const QStringList& drivers,
                    bool resourcesOk, QStringList* notes) {
  if (!f.any()) return;
  QVariantMap misc = config->value("Misc").toMap();
  QVariantMap boot = misc["Boot"].toMap();
  QVariantMap debug = misc["Debug"].toMap();

  if (f.quietBoot) {
    QVariantMap nvram = config->value("NVRAM").toMap();
    QVariantMap add = nvram["Add"].toMap();
    const QString guid = "7C436110-AB2A-4BBB-A880-FE41995C9F82";
    QVariantMap g = add[guid].toMap();
    QStringList keep;
    for (const QString& t : g["boot-args"].toString().split(' ', Qt::SkipEmptyParts))
      if (t != "-v" && t != "debug=0x100" && t != "keepsyms=1") keep << t;
    g["boot-args"] = keep.join(' ');
    add[guid] = g;
    nvram["Add"] = add;
    config->insert("NVRAM", nvram);
    debug["Target"] = 0;
    debug["AppleDebug"] = false;
    if (notes) *notes << "Finalize: quiet boot (removed -v / debug boot-args, debug logging off). Re-add -v to debug a boot problem.";
  }
  if (f.hidePicker) {
    boot["ShowPicker"] = false;
    boot["PollAppleHotKeys"] = true;  // lets you hold Esc/Space at boot to show it
    if (notes) *notes << "Finalize: boot picker hidden. Hold Esc or Space while booting to bring it back.";
  }
  if (f.openCanopy) {
    bool ready = drivers.contains("OpenCanopy.efi") && resourcesOk;
    if (!ready) {
      if (notes) *notes << "Finalize: OpenCanopy skipped (OpenCanopy.efi or the Resources folder is missing from the Database).";
    } else {
      QVariantMap uefi = config->value("UEFI").toMap();
      QVariantList list = uefi["Drivers"].toList();
      bool found = false;
      for (int i = 0; i < list.size(); ++i) {
        if (list[i].type() == QVariant::String) {
          if (list[i].toString() == "OpenCanopy.efi") found = true;
        } else {
          QVariantMap e = list[i].toMap();
          if (e["Path"].toString() == "OpenCanopy.efi") { e["Enabled"] = true; list[i] = e; found = true; }
        }
      }
      if (!found) {
        QVariantMap e;
        e["Arguments"] = "";
        e["Comment"] = "Added by EFI Builder (Finalize)";
        e["Enabled"] = true;
        e["LoadEarly"] = false;
        e["Path"] = "OpenCanopy.efi";
        list << e;
      }
      uefi["Drivers"] = list;
      config->insert("UEFI", uefi);
      boot["PickerMode"] = "External";
      if (notes) *notes << "Finalize: graphical picker (OpenCanopy) enabled.";
    }
  }
  misc["Boot"] = boot;
  misc["Debug"] = debug;
  config->insert("Misc", misc);
}

// ----------------------------------------------------------------- audio ----

AudioInfo audioInfo(const QJsonObject& r) {
  AudioInfo a;
  const QJsonObject layouts = data()["codec_layouts"].toObject()["data"].toObject();
  const QJsonObject sound = r["Sound"].toObject();
  for (auto it = sound.begin(); it != sound.end(); ++it) {
    const QJsonObject s = it.value().toObject();
    const QString id = s["Device ID"].toString();
    if (!layouts.contains(id)) continue;
    a.codecId = id;
    a.codecName = it.key();
    // Find the controller among System Devices by its device id.
    const QString ctl = s["Controller Device ID"].toString();
    if (!ctl.isEmpty()) {
      const QJsonObject sys = r["System Devices"].toObject();
      for (auto sd = sys.begin(); sd != sys.end(); ++sd)
        if (sd.value().toObject()["Device ID"].toString() == ctl) {
          a.controllerPciPath = sd.value().toObject()["PCI Path"].toString();
          break;
        }
    }
    break;  // like OpCore Simplify: the first supported codec wins
  }
  if (a.codecId.isEmpty()) return a;
  static const QStringList authors = {"Mirone", "InsanelyDeepak", "Toleda", "DalianSky"};
  bool anyRec = false;
  for (const QJsonValue& v : layouts[a.codecId].toArray()) {
    CodecLayout l;
    l.id = v.toObject()["id"].toInt();
    l.comment = v.toObject()["comment"].toString();
    for (const QString& au : authors)
      if (l.comment.contains(au)) l.recommended = true;
    anyRec = anyRec || l.recommended;
    a.layouts << l;
  }
  // OpCore Simplify picks a random recommended layout; use the first, so the
  // result is repeatable.
  for (const CodecLayout& l : a.layouts)
    if (!anyRec || l.recommended) { a.defaultId = l.id; break; }
  return a;
}

void overrideAudio(Plan* p, int layoutId) {
  if (layoutId > 0 && p->audioLayout > 0) {
    p->audioLayout = layoutId;
    p->audioForced = true;
  }
}

// ------------------------------------------------------------------ ACPI ----

bool AcpiInfo::hasEc() const {
  return has("PNP0C09") || hasBytes(QByteArray::fromHex("41d00c09"));
}

AcpiInfo readAcpiFolder(const QString& dirPath) {
  AcpiInfo a;
  a.dir = dirPath;
  const QDir d(dirPath);
  if (dirPath.isEmpty() || !d.exists()) return a;
  a.found = true;
  QStringList files;
  for (const QString& f : d.entryList(QDir::Files, QDir::Name))
    if (f.endsWith(".aml", Qt::CaseInsensitive)) files << f;
  for (const QString& f : files) {
    QFile fh(d.filePath(f));
    if (!fh.open(QIODevice::ReadOnly)) {
      a.warnings << QString("Cannot read %1").arg(f);
      continue;
    }
    const QByteArray b = fh.readAll();
    // An ACPI table starts with a 36-byte header: signature, length, ...
    if (b.size() < 36) {
      a.warnings << QString("%1 is too small to be an ACPI table").arg(f);
      continue;
    }
    const QByteArray sig = b.left(4);
    const quint32 len = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData() + 4));
    if (len != quint32(b.size())) a.warnings << QString("%1: header length %2 != file size %3").arg(f).arg(len).arg(b.size());
    a.tables << f;
    if (sig == "DSDT") a.hasDsdt = true;
    if (sig == "DSDT" || sig == "SSDT") a.blob += b;
  }
  if (a.tables.isEmpty()) a.warnings << "The folder has no ACPI tables (*.aml).";
  else if (!a.hasDsdt) a.warnings << "DSDT.aml is missing; ACPI-based choices are skipped.";
  return a;
}

namespace {
// Python-style list[a:b] membership test.
bool inSlice(const QJsonArray& arr, const QString& v, int a, int b = -1) {
  if (b < 0 || b > arr.size()) b = arr.size();
  for (int i = a; i < b; ++i)
    if (arr.at(i).toString() == v) return true;
  return false;
}
}  // namespace

void planAcpi(const QJsonObject& r, const AcpiInfo& a, Plan* p, const QStringList& avail) {
  // Drop the generic "ACPI:" hint; the analysis below replaces it.
  QStringList keep;
  for (const QString& n : p->notes)
    if (!n.startsWith("ACPI: ")) keep << n;
  p->notes = keep;
  p->acpi.clear();
  p->acpiManaged.clear();

  if (!a.found || !a.hasDsdt) {
    p->notes << "ACPI: no usable ACPI folder, so SSDTs come from the base config; check PLUG / EC / RTC / AWAC yourself.";
    return;
  }
  const QJsonObject mb = r["Motherboard"].toObject(), cpu = r["CPU"].toObject();
  const QString platform = mb["Platform"].toString(), mbName = mb["Name"].toString(),
                chipset = mb["Chipset"].toString(), code = cpu["Codename"].toString(),
                vendor = cpu["Manufacturer"].toString();
  const bool laptop = platform.contains("Laptop"), intel = vendor.contains("Intel"),
             amd = vendor.contains("AMD"), surface = mbName.contains("SURFACE");
  const bool legacyCpu = code.contains("Sandy Bridge") || code.contains("Ivy Bridge");
  const QJsonArray chips = data()["chipset_data"].toObject()["IntelChipsets"].toArray();
  const QJsonArray gens = data()["cpu_data"].toObject()["IntelCPUGenerations"].toArray();

  QStringList want, unported;
  // Decides one SSDT; `skipReason` non-empty means the firmware already provides it.
  auto decide = [&](const QString& file, bool needed, const QString& skipReason) {
    p->acpiManaged << file;
    if (!needed) return;
    if (!avail.contains(file)) { unported << file + " (not in Database)"; return; }
    if (!skipReason.isEmpty()) { p->notes << QString("ACPI: %1 skipped - %2.").arg(file, skipReason); return; }
    want << file;
  };

  // Laptop backlight / ambient light (internal panel on an iGPU, not Surface).
  QString lastGpuType;
  const QJsonObject gpus = r["GPU"].toObject();
  for (auto it = gpus.begin(); it != gpus.end(); ++it) lastGpuType = it.value().toObject()["Device Type"].toString();
  const bool panel = laptop && lastGpuType.contains("Integrated GPU") && !surface;
  decide("SSDT-ALS0.aml", panel, a.has("ALS0") ? "DSDT already defines ALS0" : QString());
  decide("SSDT-PNLF.aml", panel, a.has("PNLF") ? "DSDT already defines PNLF" : QString());

  // CPU power management. Sandy/Ivy Bridge need the legacy generator.
  if (legacyCpu) unported << "PM (Legacy) for Sandy/Ivy Bridge";
  bool cpuScope = a.has("_PR_") || a.has("CPU0") || a.has("PR00") || a.has("CP00") || a.has("SCK0");
  decide("SSDT-PLUG.aml", !legacyCpu, QString());
  if (!legacyCpu && !cpuScope)
    p->notes << "ACPI: no known processor scope (_PR_/CPU0/PR00/CP00/SCK0) found in your tables, so SSDT-PLUG may not attach; check it with iasl.";

  // Embedded controller + USBX. Desktops need a fake EC; use the multi-name
  // variant when the LPC bridge is not called LPCB.
  const bool lpcb = a.has("LPCB");
  QString ec = "SSDT-EC-USBX.aml";
  if (laptop && a.hasEc() && avail.contains("SSDT-EC-USBX_Laptop.aml")) ec = "SSDT-EC-USBX_Laptop.aml";
  else if (!lpcb && avail.contains("SSDT-EC_USBX_Desktop.aml")) ec = "SSDT-EC_USBX_Desktop.aml";
  const QStringList ecVariants = QStringList() << "SSDT-EC.aml" << "SSDT-EC-USBX.aml"
                                               << "SSDT-EC-USBX_Laptop.aml" << "SSDT-EC_USBX_Desktop.aml";
  for (const QString& f : ecVariants) decide(f, f == ec, QString());
  if (!lpcb && ec == "SSDT-EC-USBX.aml")
    p->notes << "ACPI: no LPCB device in your DSDT; SSDT-EC-USBX may not attach.";

  // Chipset specific fixes.
  const bool hasChipset = intel && !chipset.isEmpty();
  decide("SSDT-PMC.aml", hasChipset && inSlice(chips, chipset, 100, 112), a.has("PMCR") ? "DSDT already defines PMCR" : QString());
  decide("SSDT-UNC.aml", hasChipset && (inSlice(chips, chipset, 27, 28) || inSlice(chips, chipset, 62, 64)),
         a.has("UNC0") ? "DSDT already defines UNC0" : QString());
  decide("SSDT-SBUS-MCHC.aml", intel, a.has("BUS0") ? "DSDT already defines BUS0" : QString());

  // RTC / AWAC.
  const bool rtcRule = hasChipset && (inSlice(chips, chipset, 62, 64) || inSlice(chips, chipset, 90, 100));
  const bool awacRule = amd || inSlice(gens, code, 0, 40);
  const bool hasAwac = a.has("ACPI000E");
  const bool hasRtc = a.hasBytes(QByteArray::fromHex("41d00b00")) || a.has("PNP0B00");
  decide("SSDT-AWAC-DISABLE.aml", awacRule && hasAwac, QString());
  decide("SSDT-RTC0.aml", (rtcRule || awacRule) && !hasRtc, QString());

  // Sandy/Ivy Bridge Management Engine.
  bool imei = false;
  const QJsonObject sys = r["System Devices"].toObject();
  for (auto it = sys.begin(); it != sys.end(); ++it) {
    const QString id = it.value().toObject()["Device ID"].toString();
    if ((code.contains("Sandy Bridge") && id == "8086-1E3A") || (code.contains("Ivy Bridge") && id == "8086-1C3A")) imei = true;
  }
  decide("SSDT-IMEI.aml", imei, QString());

  // SSDT-RTC0 and SSDT-RTC0-RANGE are alternatives; never leave both on.
  if (want.contains("SSDT-RTC0.aml")) p->acpiManaged << "SSDT-RTC0-RANGE.aml";

  p->acpi = want;

  // Patches OpCore Simplify generates with iasl that are not available here.
  QStringList hand;
  if (laptop) hand << "battery status (BATP)" << "OSI override (XOSI + _OSI rename)";
  if (laptop && inSlice(gens, code, 50)) hand << "HPET fix";
  if (mbName.contains("HP ")) hand << "CMOS reset (HP)";
  if (amd || (intel && inSlice(chips, chipset, 112))) hand << "USB reset";
  if (surface) hand << "Surface patch";
  if (intel && inSlice(chips, chipset, chips.size() - 7)) hand << "RCSP (reserved chipset resources)";
  {
    // HEDT parts need an APIC patch (same ranges as OpCore Simplify).
    const QString proc = cpu["Processor Name"].toString();
    bool hedt = false;
    if (inSlice(gens, code, 45, 66))
      for (const char* suf : {"-X", "-P", "-W", "-E", "-EP", "-EX"}) hedt = hedt || code.endsWith(suf);
    if (inSlice(gens, code, 66)) hedt = proc.contains("Xeon");
    if (hedt) hand << "APIC (HEDT)";
    const QJsonObject net = r["Network"].toObject();
    bool allUsb = !net.isEmpty();
    for (auto it = net.begin(); it != net.end(); ++it)
      allUsb = allUsb && it.value().toObject()["Bus Type"].toString() == "USB";
    if (allUsb) hand << "RMNE (USB-only network)";
  }
  const QJsonObject input = r["Input"].toObject();
  for (auto it = input.begin(); it != input.end(); ++it)
    if (intel && it.value().toObject()["Device Type"].toString().contains("I2C")) { hand << "GPI0 (I2C trackpad)"; break; }
  hand << unported;
  if (!hand.isEmpty())
    p->notes << "ACPI: not generated here (OpCore Simplify builds these with iasl): " + hand.join(", ") + ".";
  if (!a.warnings.isEmpty()) p->notes << "ACPI folder: " + a.warnings.join("; ");
}

// ------------------------------------------------------------------ plan ----

Plan makePlan(const QJsonObject& r, const Compatibility& c, const QString& targetMax) {
  Plan p;
  const QJsonObject cpu = r["CPU"].toObject();
  const QString platform = r["Motherboard"].toObject()["Platform"].toString();
  const QString cn = codename(cpu);
  const bool laptop = platform == "Laptop";
  const bool intel = cpu["Manufacturer"].toString() == "Intel";

  struct Gen { const char* key; const char* tpl; const char* desk; const char* lap; };
  static const Gen gens[] = {
      {"Clarkdale", "Desktop_01stGen_Clarkdale_iMac11,2", "iMac11,2", "MacBookPro8,1"},
      {"Westmere", "Desktop_01stGen_Clarkdale_iMac11,2", "iMac11,2", "MacBookPro8,1"},
      {"Nehalem", "Desktop_01stGen_Clarkdale_iMac11,2", "iMac11,2", "MacBookPro8,1"},
      {"Arrandale", "Desktop_01stGen_Clarkdale_iMac11,2", "iMac11,2", "MacBookPro8,1"},
      {"Sandy Bridge", "Desktop_02ndGen_Sandy_Bridge_iMac12,2", "iMac12,2", "MacBookPro8,1"},
      {"Ivy Bridge", "Desktop_03rdGen_Ivy_Bridge_iMac13,2", "iMac13,2", "MacBookPro10,1"},
      {"Haswell", "Desktop_04thGen_Haswell_iMac15,1", "iMac15,1", "MacBookPro11,1"},
      {"Broadwell", "Desktop_05thGen_Broadwell_iMac16,2", "iMac16,2", "MacBookPro12,1"},
      {"Skylake", "Desktop_06thGen_Skylake_iMac17,1", "iMac17,1", "MacBookPro13,1"},
      {"Kaby Lake", "Desktop_07thGen_Kaby_Lake_iMac18,1", "iMac18,1", "MacBookPro14,1"},
      {"Coffee Lake", "Desktop_08th-9thGen_Coffee_Lake_iMac19,1", "iMac19,1", "MacBookPro15,1"},
      {"Comet Lake", "Desktop_10thGen_Comet_Lake_iMac20,1", "iMac20,1", "MacBookPro16,1"},
      {"Rocket Lake", "Desktop_11thGen_Rocket_Lake_iMacPro1,1", "iMacPro1,1", "MacBookPro16,1"},
  };
  p.templateName = "SampleCustom";
  if (intel) {
    for (const Gen& g : gens)
      if (cn.contains(g.key, Qt::CaseInsensitive)) {
        p.templateName = g.tpl;
        p.smbios = laptop ? g.lap : g.desk;
        break;
      }
  }
  if (p.smbios.isEmpty()) {
    p.notes << QString("No matching base config / SMBIOS for CPU codename \"%1\"; using SampleCustom "
                       "- choose a Mac model and set platform properties manually.").arg(cn);
    p.smbios = laptop ? "MacBookPro16,1" : "iMacPro1,1";
  }
  if (laptop)
    p.notes << "Base configs are desktop-oriented; laptop needs touchpad/keyboard/battery ACPI work.";
  if (!intel)
    p.notes << "AMD CPU: the AMD Vanilla kernel patches are NOT applied; add them from AMD_Vanilla.";

  // Kexts ---------------------------------------------------------------
  QSet<QString> k{"Lilu", "VirtualSMC", "RestrictEvents"};
  if (intel) k << "SMCProcessor";
  k << (laptop ? "SMCBatteryManager" : "SMCSuperIO");
  bool anyGpu = false;
  for (const auto& d : c.devices) if (d.kind == "GPU" && d.range.supported()) anyGpu = true;
  if (anyGpu) k << "WhateverGreen";

  // AppleALC + layout id (applied as layout-id on the controller, else alcid=).
  {
    const AudioInfo au = audioInfo(r);
    if (!au.layouts.isEmpty()) {
      k << "AppleALC";
      // Tahoe dropped AppleHDA: SimpleHDA restores it without root patches.
      if (darwinMajor(targetMax) >= 25) k << "SimpleHDA";
      p.audioLayout = au.defaultId;
      p.audioPciPath = au.controllerPciPath;
    }
  }

  // Ethernet / Wi-Fi / Bluetooth.
  static const QList<QPair<const char*, const char*>> ethMap = {
      {"IntelMausiIDs", "IntelMausi"},       {"RealtekRTL8111IDs", "RealtekRTL8111"},
      {"RealtekRTL8125IDs", "LucyRTL8125Ethernet"}, {"AppleIGBIDs", "AppleIGB"},
      {"AtherosE2200IDs", "AtherosE2200Ethernet"}, {"IntelI22XIDs", "AppleIGC"}};
  const QJsonObject net = r["Network"].toObject();
  for (auto it = net.begin(); it != net.end(); ++it) {
    const QString id = it.value().toObject()["Device ID"].toString();
    for (const auto& e : ethMap) if (inPci(e.first, id)) k << e.second;
    if (inPci("IntelWiFiIDs", id)) { k << "AirportItlwm"; }
    if (inPci("BroadcomWiFiIDs", id)) { k << "AirportBrcmFixup"; p.notes << "Broadcom Wi-Fi: also needs BrcmFirmwareData/BrcmPatchRAM."; }
  }
  const QJsonObject bt = r["Bluetooth"].toObject();
  for (auto it = bt.begin(); it != bt.end(); ++it) {
    const QString id = it.value().toObject()["Device ID"].toString();
    if (id.startsWith("8086") || id.startsWith("8087")) { k << "IntelBluetoothFirmware" << "BlueToolFixup"; }
    else if (id.startsWith("0A5C") || id.startsWith("413C")) k << "BrcmPatchRAM3";
  }
  // Input.
  bool i2c = false, ps2 = laptop;
  const QJsonObject input = r["Input"].toObject();
  for (auto it = input.begin(); it != input.end(); ++it) {
    const QJsonObject o = it.value().toObject();
    const QString blob = it.key() + o["Device"].toString() + o["Bus Type"].toString();
    if (blob.contains("I2C", Qt::CaseInsensitive)) i2c = true;
    if (blob.contains("PS/2", Qt::CaseInsensitive) || blob.contains("PS2", Qt::CaseInsensitive)) ps2 = true;
  }
  if (ps2) k << "VoodooPS2Controller";
  if (i2c) { k << "VoodooI2C" << "VoodooI2CHID"; p.notes << "I2C touchpad: GPIO pinning/SSDT-GPIO may be required."; }
  if (laptop) k << "BrightnessKeys";
  // Storage.
  const QJsonObject stoCtl = r["Storage Controllers"].toObject();
  for (auto it = stoCtl.begin(); it != stoCtl.end(); ++it)
    if (QJsonDocument(it.value().toObject()).toJson().contains("NVM")) { k << "NVMeFix"; break; }

  p.kexts = k.values();
  std::sort(p.kexts.begin(), p.kexts.end());

  // Boot args -----------------------------------------------------------
  const QJsonObject gpuObj = r["GPU"].toObject();
  for (auto it = gpuObj.begin(); it != gpuObj.end(); ++it) {
    const QJsonObject g = it.value().toObject();
    if (g["Manufacturer"].toString() == "AMD" && g["Codename"].toString().contains("Navi") &&
        !p.bootArgsAdd.contains("agdpmod"))
      p.bootArgsAdd += " agdpmod=pikera";
  }
  if (!targetMax.isEmpty() && darwinMajor(targetMax) >= 22 && !intel)
    p.notes << "AMD CPU on Ventura+: ensure the AMD kernel patches match the macOS version.";

  p.notes << "USB: map ports with USBToolBox/UTBMap before relying on USB boot/install.";
  p.notes << "ACPI: SSDTs come from the base config. Load the report's ACPI folder to select them from your own tables.";
  for (const auto& w : c.warnings) p.notes << w;
  return p;
}

QVariantMap applyPlan(QVariantMap config, const Plan& plan, const QStringList& available,
                      QStringList* missing, const QStringList& disableKexts,
                      const QString& kextDir) {
  // Kexts that this planner owns: enabled iff the hardware needs them.
  static const QSet<QString> managed{"WhateverGreen", "AppleALC", "NVMeFix", "RestrictEvents"};
  QVariantMap kernel = config["Kernel"].toMap();
  QVariantList add = kernel["Add"].toList();
  QSet<QString> present;
  for (int i = 0; i < add.size(); ++i) {
    QVariantMap e = add[i].toMap();
    QString base = e["BundlePath"].toString();
    // Nested plugins (Foo.kext/Contents/PlugIns/Bar.kext) ship inside their parent.
    QString top = base.section('/', 0, 0);
    top.chop(5);  // ".kext"
    base = base.section('/', -1);
    base.chop(5);
    present.insert(base);
    if (plan.kexts.contains(base)) {
      // Never enable an entry whose folder will not exist in the EFI.
      const bool have = available.contains(top);
      e["Enabled"] = have;
      if (!have && missing && !missing->contains(base)) *missing << base;
    } else if (managed.contains(base) || disableKexts.contains(base)) e["Enabled"] = false;
    add[i] = e;
  }
  for (const QString& name : plan.kexts) {
    if (present.contains(name)) continue;
    if (available.contains(name)) {
      QVariantMap e;
      e["Arch"] = "Any";
      e["BundlePath"] = name + ".kext";
      e["Comment"] = "Added by EFI builder";
      e["Enabled"] = true;
      // Some kexts (e.g. injectors, UTBDefault) have no binary.
      const bool hasBin = kextDir.isEmpty() ||
                          QFile::exists(kextDir + "/" + name + ".kext/Contents/MacOS/" + name);
      e["ExecutablePath"] = hasBin ? "Contents/MacOS/" + name : QString();
      e["MaxKernel"] = "";
      e["MinKernel"] = "";
      e["PlistPath"] = "Contents/Info.plist";
      add.append(e);
    } else if (missing) {
      *missing << name;
    }
  }
  kernel["Add"] = add;
  config["Kernel"] = kernel;

  QVariantMap nvram = config["NVRAM"].toMap();
  QVariantMap nadd = nvram["Add"].toMap();
  const QString guid = "7C436110-AB2A-4BBB-A880-FE41995C9F82";
  QVariantMap g = nadd[guid].toMap();
  QString args = g["boot-args"].toString();
  bool hasLayoutId = false;
  {
    const QVariantMap dadd = config["DeviceProperties"].toMap()["Add"].toMap();
    for (auto it = dadd.begin(); it != dadd.end(); ++it)
      if (!it.key().startsWith('#') && it.value().toMap().contains("layout-id")) hasLayoutId = true;
  }
  const bool applyAudio = plan.audioLayout > 0 && (!hasLayoutId || plan.audioForced);
  QString extraArgs = plan.bootArgsAdd;
  // Without a known audio controller there is no PCI path for layout-id; use alcid.
  if (applyAudio && plan.audioPciPath.isEmpty())
    extraArgs += QString(" alcid=%1").arg(plan.audioLayout);
  for (const QString& tok : extraArgs.split(' ', Qt::SkipEmptyParts)) {
    const QString key = tok.section('=', 0, 0);
    if (!QRegularExpression("(^|\\s)" + QRegularExpression::escape(key) + "(=|\\s|$)").match(args).hasMatch())
      args += (args.isEmpty() ? "" : " ") + tok;
  }
  g["boot-args"] = args;
  nadd[guid] = g;
  nvram["Add"] = nadd;
  config["NVRAM"] = nvram;

  if (applyAudio && !plan.audioPciPath.isEmpty()) {
    QVariantMap dp = config["DeviceProperties"].toMap();
    QVariantMap dadd = dp["Add"].toMap();
    QVariantMap dev = dadd[plan.audioPciPath].toMap();
    dev["layout-id"] = plan.audioLayout;
    dadd[plan.audioPciPath] = dev;
    dp["Add"] = dadd;
    config["DeviceProperties"] = dp;
  }

  if (!plan.acpiManaged.isEmpty()) {
    QVariantMap acpi = config["ACPI"].toMap();
    QVariantList add = acpi["Add"].toList();
    QSet<QString> seen;
    for (int i = 0; i < add.size(); ++i) {
      QVariantMap e = add[i].toMap();
      const QString path = e["Path"].toString();
      seen.insert(path);
      if (plan.acpiManaged.contains(path)) e["Enabled"] = plan.acpi.contains(path);
      add[i] = e;
    }
    for (const QString& f : plan.acpi) {
      if (seen.contains(f)) continue;
      QVariantMap e;
      e["Comment"] = "Added by EFI Builder from the ACPI analysis";
      e["Enabled"] = true;
      e["Path"] = f;
      add << e;
    }
    acpi["Add"] = add;
    config["ACPI"] = acpi;
  }

  if (plan.secureBootOff) {
    QVariantMap misc = config["Misc"].toMap();
    QVariantMap sec = misc["Security"].toMap();
    sec["SecureBootModel"] = "Disabled";
    misc["Security"] = sec;
    config["Misc"] = misc;
  }
  if (!plan.smbios.isEmpty()) {
    QVariantMap pi = config["PlatformInfo"].toMap();
    QVariantMap gen = pi["Generic"].toMap();
    gen["SystemProductName"] = plan.smbios;
    pi["Generic"] = gen;
    config["PlatformInfo"] = pi;
  }
  return config;
}

int pruneUnused(QVariantMap* config) {
  int removed = 0;
  auto prune = [&](const QString& section) {
    QVariantMap sec = config->value(section).toMap();
    const QVariantList add = sec.value("Add").toList();
    QVariantList keep;
    for (const QVariant& v : add) {
      if (v.toMap().value("Enabled").toBool()) keep << v;
      else ++removed;
    }
    if (keep.size() == add.size()) return;
    sec["Add"] = keep;
    config->insert(section, sec);
  };
  prune("ACPI");
  prune("Kernel");
  return removed;
}

bool audioNeedsTahoeChoice(const QJsonObject& r, const QString& target) {
  return darwinMajor(target) >= 25 && !audioInfo(r).layouts.isEmpty();
}

void overrideAudioKext(Plan* p, bool useOclp) {
  if (!useOclp) return;
  p->kexts.removeAll("SimpleHDA");
  if (!p->kexts.contains("AMFIPass")) p->kexts << "AMFIPass";  // RestrictEvents is always in the plan
  p->secureBootOff = true;
  if (!p->bootArgsAdd.contains("-amfipassbeta")) p->bootArgsAdd += " -amfipassbeta";
  p->notes << "Audio on macOS 26 Tahoe uses OpenCore Legacy Patcher root patches (AppleHDA). "
              "Run OCLP on the installed system and apply the audio patch.";
}

QList<GpuCombo> gpuCombinations(const QJsonObject& r, const Compatibility& c) {
  const QJsonObject gpus = r["GPU"].toObject();
  const QStringList names = gpus.keys();
  if (names.size() < 2) return {};
  QList<int> apu, navi22, navi, intel, other;
  for (int i = 0; i < names.size(); ++i) {
    const QJsonObject g = gpus[names[i]].toObject();
    const QString man = g["Manufacturer"].toString(), type = g["Device Type"].toString(),
                  code = g["Codename"].toString();
    if (man == "AMD") {
      if (type == "Integrated GPU") { apu << i; continue; }
      if (type == "Discrete GPU" && code.startsWith("Navi")) { (code == "Navi 22" ? navi22 : navi) << i; continue; }
    } else if (man == "Intel") { intel << i; continue; }
    other << i;
  }
  auto cat = [](QList<int> a, const QList<int>& b) { a += b; return a; };
  QList<QList<int>> groups;
  if (!apu.isEmpty()) groups << cat(apu, other);
  if (!navi22.isEmpty()) groups << cat(navi22, other);
  if (!navi.isEmpty() || !intel.isEmpty() || !other.isEmpty()) groups << cat(cat(navi, intel), other);

  QHash<QString, Range> rng;
  for (const auto& d : c.devices) if (d.kind == "GPU") rng[d.name] = d.range;
  QList<GpuCombo> out;
  QSet<QString> seen;
  for (const QList<int>& grp : groups) {
    QList<int> g = grp;
    std::sort(g.begin(), g.end());
    for (int mask = 1; mask < (1 << g.size()); ++mask) {
      GpuCombo gc;
      bool first = true, needsPatch = false;
      for (int j = 0; j < g.size(); ++j) {
        if (!((mask >> j) & 1)) continue;
        const QString n = names[g[j]];
        gc.names << n;
        const Range one = rng.value(n);
        if (!one.supported()) { needsPatch = true; continue; }
        if (first) { gc.range = one; first = false; continue; }
        // Intersection: lowest top version, highest bottom version.
        if (darwinLess(one.maxV, gc.range.maxV)) gc.range.maxV = one.maxV;
        if (darwinLess(gc.range.minV, one.minV)) gc.range.minV = one.minV;
      }
      // Like OpCore Simplify: a GPU that macOS does not support natively (needs
      // OCLP/root patches) is only offered on its own, never combined with others.
      if (needsPatch && gc.names.size() > 1) continue;
      const QString key = gc.names.join('\n');
      if (seen.contains(key)) continue;
      seen.insert(key);
      out << gc;
    }
  }
  std::sort(out.begin(), out.end(), [](const GpuCombo& a, const GpuCombo& b) {
    if (a.names.size() != b.names.size()) return a.names.size() < b.names.size();
    return a.names.join('\n') < b.names.join('\n');
  });
  return out;
}

QJsonObject withGpus(const QJsonObject& r, const QStringList& keep) {
  QJsonObject out = r, g;
  const QJsonObject all = r["GPU"].toObject();
  for (auto it = all.begin(); it != all.end(); ++it)
    if (keep.contains(it.key())) g[it.key()] = it.value();
  out["GPU"] = g;
  return out;
}

int disableGpus(QVariantMap* config, const QJsonObject& full, const QJsonObject& active) {
  const QJsonObject all = full["GPU"].toObject(), kept = active["GPU"].toObject();
  QVariantMap dp = config->value("DeviceProperties").toMap();
  QVariantMap add = dp.value("Add").toMap();
  int n = 0;
  for (auto it = all.begin(); it != all.end(); ++it) {
    if (kept.contains(it.key())) continue;
    const QString path = it.value().toObject()["PCI Path"].toString();
    if (path.isEmpty()) continue;
    QVariantMap dev = add.value(path).toMap();
    dev["disable-gpu"] = true;
    add[path] = dev;
    ++n;
  }
  if (n) {
    dp["Add"] = add;
    config->insert("DeviceProperties", dp);
  }
  return n;
}

}  // namespace EfiBuilder
