#include "sysinfo.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QDateTime>
#include <QRegularExpression>
#include <QSysInfo>

namespace {

QString run(const QString& prog, const QStringList& args, int timeoutMs = 8000) {
  QProcess p;
  p.start(prog, args);
  if (!p.waitForStarted(2000)) return QString();
  if (!p.waitForFinished(timeoutMs)) {
    p.kill();
    p.waitForFinished(1000);
    return QString();
  }
  return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void add(QVariantList& out, const QString& sec, const QString& key, const QString& val) {
  if (!val.isEmpty()) out.append(QStringList{sec, key, val});
}

QString humanBytes(qulonglong b) {
  const double gb = b / (1024.0 * 1024.0 * 1024.0);
  return gb >= 1 ? QString::number(gb, 'f', gb < 10 ? 1 : 0) + " GB" : QString::number(b / (1024 * 1024)) + " MB";
}

// SPDisplaysDataType gives the vendor as e.g. "sppci_vendor_amd", not as a PCI id.
QString vendorId(const QString& v) {
  if (v.contains("amd", Qt::CaseInsensitive) || v.contains("ati", Qt::CaseInsensitive)) return "0x1002";
  if (v.contains("intel", Qt::CaseInsensitive)) return "0x8086";
  if (v.contains("nvidia", Qt::CaseInsensitive)) return "0x10de";
  return QString();
}

#ifdef Q_OS_MAC
void collectMac(QVariantList& o) {
  const QString sys = QObject::tr("System"), cpu = QObject::tr("Processor"), mem = QObject::tr("Memory"),
                gpu = QObject::tr("Graphics"), oc = QObject::tr("Boot");
  add(o, sys, QObject::tr("Model identifier"), run("sysctl", {"-n", "hw.model"}));
  add(o, sys, QObject::tr("macOS"), run("sw_vers", {"-productName"}) + " " + run("sw_vers", {"-productVersion"}) +
                                       " (" + run("sw_vers", {"-buildVersion"}) + ")");
  add(o, sys, QObject::tr("Kernel"), QSysInfo::kernelVersion());

  // One system_profiler run for every data type: each separate run costs seconds.
  const QJsonObject sp = QJsonDocument::fromJson(
                             run("system_profiler",
                                 {"SPHardwareDataType", "SPMemoryDataType", "SPDisplaysDataType", "SPNVMeDataType",
                                  "SPSerialATADataType", "SPNetworkDataType", "SPAudioDataType", "-json"},
                                 40000)
                                 .toUtf8())
                             .object();
  const QJsonObject h = sp["SPHardwareDataType"].toArray().first().toObject();
  add(o, sys, QObject::tr("Serial number"), h["serial_number"].toString());
  add(o, sys, QObject::tr("Board ID"), run("sh", {"-c", "ioreg -d2 -c IOPlatformExpertDevice | awk -F'\"' '/board-id/{print $4}'"}));
  add(o, sys, QObject::tr("Firmware"), h["boot_rom_version"].toString());
  const QString bt = run("sysctl", {"-n", "kern.boottime"});  // "{ sec = 1793486694, usec = ... } ..."
  QRegularExpressionMatch bm = QRegularExpression("sec = (\\d+)").match(bt);
  if (bm.hasMatch()) {
    const qint64 up = QDateTime::currentSecsSinceEpoch() - bm.captured(1).toLongLong();
    if (up > 0)
      add(o, sys, QObject::tr("Uptime"),
          up >= 86400 ? QObject::tr("%1 d %2 h").arg(up / 86400).arg(up % 86400 / 3600)
                      : QObject::tr("%1 h %2 min").arg(up / 3600).arg(up % 3600 / 60));
  }

  add(o, cpu, QObject::tr("Name"), run("sysctl", {"-n", "machdep.cpu.brand_string"}));
  const QString cores = run("sysctl", {"-n", "hw.physicalcpu"}), threads = run("sysctl", {"-n", "hw.logicalcpu"});
  if (!cores.isEmpty()) add(o, cpu, QObject::tr("Cores / threads"), cores + " / " + threads);
  const QString feat = run("sysctl", {"-n", "machdep.cpu.features"}) + " " + run("sysctl", {"-n", "machdep.cpu.leaf7_features"});
  if (feat.trimmed().size()) {
    QStringList f;
    for (const char* k : {"SSE4.2", "AVX1.0", "AVX2", "AES"})
      if (feat.split(' ').contains(k)) f << k;
    add(o, cpu, QObject::tr("Features"), f.join(", "));
  }

  bool ok = false;
  const qulonglong mb = run("sysctl", {"-n", "hw.memsize"}).toULongLong(&ok);
  if (ok) add(o, mem, QObject::tr("Installed"), humanBytes(mb));
  for (const QJsonValue& g : sp["SPMemoryDataType"].toArray())
    for (const QJsonValue& v : g.toObject()["_items"].toArray()) {
      const QJsonObject d = v.toObject();
      const QString size = d["dimm_size"].toString();
      if (size.isEmpty() || size.contains("empty", Qt::CaseInsensitive)) continue;
      QStringList parts{size, d["dimm_type"].toString(), d["dimm_speed"].toString(), d["dimm_manufacturer"].toString()};
      parts.removeAll(QString());
      add(o, mem, d["_name"].toString().section('/', -1), parts.join("  "));
    }

  int n = 0, nd = 0;
  for (const QJsonValue& v : sp["SPDisplaysDataType"].toArray()) {
    const QJsonObject g = v.toObject();
    const QString label = QObject::tr("GPU %1").arg(++n);
    QString name = g["sppci_model"].toString();
    QString ids;
    const QString vid = vendorId(g["spdisplays_vendor"].toString()), did = g["spdisplays_device-id"].toString();
    if (!did.isEmpty()) ids = (vid.isEmpty() ? QString() : vid + ":") + did;
    QString vram = g["spdisplays_vram"].toString();
    if (vram.isEmpty()) vram = g["spdisplays_vram_shared"].toString();
    QStringList parts{name};
    if (!ids.isEmpty()) parts << "[" + ids + "]";
    if (!vram.isEmpty()) parts << "- " + vram;
    add(o, gpu, label, parts.join(' '));
    for (const QJsonValue& dv : g["spdisplays_ndrvs"].toArray()) {
      const QJsonObject d = dv.toObject();
      QString res = d["_spdisplays_resolution"].toString();
      if (res.isEmpty()) res = d["spdisplays_resolution"].toString();
      add(o, QObject::tr("Displays"), QObject::tr("Display %1").arg(++nd),
          d["_name"].toString() + (res.isEmpty() ? QString() : "  " + res) +
              (d["spdisplays_main"].toString() == "spdisplays_yes" ? QObject::tr("  (main)") : QString()));
    }
  }

  // Storage: NVMe and SATA drives, then the boot volume and EFI partitions.
  const QString st = QObject::tr("Storage");
  for (const QJsonValue& c : sp["SPNVMeDataType"].toArray())
    for (const QJsonValue& v : c.toObject()["_items"].toArray()) {
      const QJsonObject d = v.toObject();
      add(o, st, QObject::tr("NVMe"), d["_name"].toString() + "  " + d["size"].toString() +
                                          (d["spnvme_linkwidth"].toString().isEmpty() ? QString() : "  " + d["spnvme_linkwidth"].toString()));
    }
  for (const QJsonValue& c : sp["SPSerialATADataType"].toArray())
    for (const QJsonValue& v : c.toObject()["_items"].toArray()) {
      const QJsonObject d = v.toObject();
      add(o, st, QObject::tr("SATA"), d["_name"].toString().trimmed() + "  " + d["size"].toString() +
                                          (d["spsata_medium_type"].toString().isEmpty() ? QString() : "  " + d["spsata_medium_type"].toString()));
    }
  const QString vol = run("diskutil", {"info", "/"});
  auto field = [&vol](const char* k) {
    QRegularExpressionMatch m = QRegularExpression(QString("^\\s*%1:\\s*(.+)$").arg(k), QRegularExpression::MultilineOption).match(vol);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
  };
  QStringList bv{field("Volume Name"), field("File System Personality"), field("Protocol")};
  bv.removeAll(QString());
  add(o, st, QObject::tr("Boot volume"), bv.join("  "));
  QStringList esp;
  QRegularExpressionMatchIterator it = QRegularExpression("\\sEFI\\s+EFI\\s+([\\d.]+ \\w+)\\s+(disk\\w+)").globalMatch(run("diskutil", {"list"}));
  while (it.hasNext()) {
    QRegularExpressionMatch m = it.next();
    esp << m.captured(2) + " (" + m.captured(1) + ")";
  }
  add(o, st, QObject::tr("EFI partitions"), esp.join(", "));

  // Network adapters (skip VPN/virtual ones: they have no hardware type).
  for (const QJsonValue& v : sp["SPNetworkDataType"].toArray()) {
    const QJsonObject d = v.toObject();
    const QString hwType = d["hardware"].toString();
    if (hwType.isEmpty()) continue;
    const QString mac = d["Ethernet"].toObject()["MAC Address"].toString();
    add(o, QObject::tr("Network"), d["_name"].toString(), d["interface"].toString() + (mac.isEmpty() ? QString() : "  " + mac));
  }

  // Audio: the default output device.
  for (const QJsonValue& g : sp["SPAudioDataType"].toArray())
    for (const QJsonValue& v : g.toObject()["_items"].toArray()) {
      const QJsonObject d = v.toObject();
      if (d["coreaudio_default_audio_output_device"].toString() == "spaudio_yes")
        add(o, QObject::tr("Audio"), QObject::tr("Default output"), d["_name"].toString());
    }

  const QString ocVer = run("nvram", {"4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:opencore-version"});
  if (ocVer.contains('\t')) add(o, oc, QObject::tr("OpenCore"), ocVer.section('\t', 1).trimmed());
  else add(o, oc, QObject::tr("OpenCore"), QObject::tr("not detected (no opencore-version in NVRAM)"));
  const QString sip = run("csrutil", {"status"});
  // csrutil prints a multi-line report when SIP is partly off; the first line is the verdict.
  add(o, oc, QObject::tr("SIP"), sip.section('\n', 0, 0).section(':', 1).trimmed().remove(QRegularExpression("\\.$")));
  const QString csr = run("nvram", {"csr-active-config"});
  if (csr.contains('\t')) add(o, oc, QObject::tr("csr-active-config"), SysInfoCollector::nvramBytesToHex(csr.section('\t', 1).trimmed()));
  const QString bootArgs = run("nvram", {"boot-args"});
  if (bootArgs.contains('\t')) add(o, oc, QObject::tr("boot-args"), bootArgs.section('\t', 1).trimmed());

  const QString sec = QObject::tr("Security");
  add(o, sec, QObject::tr("FileVault"), run("fdesetup", {"status"}).section('\n', 0, 0).remove(QRegularExpression("\\.$")));
  add(o, sec, QObject::tr("Gatekeeper"), run("spctl", {"--status"}));

  // Third-party kexts that are loaded now (anything not com.apple.*), e.g. Lilu, WhateverGreen.
  QStringList kx;
  const QString kl = run("kmutil", {"showloaded", "--list-only"}, 15000);
  QRegularExpressionMatchIterator ki = QRegularExpression("([A-Za-z0-9_.\\-]+) \\(([0-9][0-9A-Za-z.\\-]*)\\)").globalMatch(kl);
  while (ki.hasNext()) {
    QRegularExpressionMatch m = ki.next();
    if (m.captured(1).startsWith("com.apple.")) continue;
    const QString entry = m.captured(1).section('.', -1) + " " + m.captured(2);
    if (!kx.contains(entry)) kx << entry;
  }
  kx.sort(Qt::CaseInsensitive);
  add(o, QObject::tr("Loaded kexts"), QObject::tr("Non-Apple (%1)").arg(kx.size()), kx.join(", "));
}
#endif

#ifdef Q_OS_LINUX
void collectLinux(QVariantList& o) {
  const QString sys = QObject::tr("System"), cpu = QObject::tr("Processor"), mem = QObject::tr("Memory");
  QFile f("/proc/cpuinfo");
  if (f.open(QIODevice::ReadOnly)) {
    const QString t = QString::fromUtf8(f.readAll());
    QRegularExpressionMatch m = QRegularExpression("model name\\s*:\\s*(.+)").match(t);
    if (m.hasMatch()) add(o, cpu, QObject::tr("Name"), m.captured(1).trimmed());
    add(o, cpu, QObject::tr("Threads"), QString::number(t.count(QRegularExpression("^processor", QRegularExpression::MultilineOption))));
  }
  QFile mf("/proc/meminfo");
  if (mf.open(QIODevice::ReadOnly)) {
    QRegularExpressionMatch m = QRegularExpression("MemTotal:\\s*(\\d+) kB").match(QString::fromUtf8(mf.readAll()));
    if (m.hasMatch()) add(o, mem, QObject::tr("Installed"), humanBytes(m.captured(1).toULongLong() * 1024));
  }
  add(o, sys, QObject::tr("Kernel"), QSysInfo::kernelVersion());
}
#endif

#ifdef Q_OS_WIN
QString wmic(const QString& q) {  // "cpu get name" -> value of the first data line
  const QStringList l = run("wmic", q.split(' '), 10000).split('\n', QString::SkipEmptyParts);
  return l.size() > 1 ? l.at(1).trimmed() : QString();
}
void collectWin(QVariantList& o) {
  const QString sys = QObject::tr("System"), cpu = QObject::tr("Processor"), mem = QObject::tr("Memory"),
                gpu = QObject::tr("Graphics");
  add(o, sys, QObject::tr("Model"), wmic("computersystem get model"));
  add(o, sys, QObject::tr("Kernel"), QSysInfo::kernelVersion());
  add(o, cpu, QObject::tr("Name"), wmic("cpu get name"));
  add(o, cpu, QObject::tr("Cores / threads"),
      wmic("cpu get NumberOfCores") + " / " + wmic("cpu get NumberOfLogicalProcessors"));
  bool ok = false;
  const qulonglong b = wmic("computersystem get TotalPhysicalMemory").toULongLong(&ok);
  if (ok) add(o, mem, QObject::tr("Installed"), humanBytes(b));
  add(o, gpu, QObject::tr("GPU"), wmic("path win32_VideoController get name"));
}
#endif

}  // namespace

QString SysInfoCollector::nvramBytesToHex(const QString& s) {
  QString hex;
  const QByteArray b = s.toLatin1();
  for (int i = 0; i < b.size(); i++) {
    bool ok = false;
    const int v = (b[i] == '%' && i + 2 < b.size()) ? b.mid(i + 1, 2).toInt(&ok, 16) : 0;
    if (ok) { hex += QString("%1").arg(v, 2, 16, QChar('0')); i += 2; }
    else hex += QString("%1").arg(static_cast<uchar>(b[i]), 2, 16, QChar('0'));
  }
  return hex.toUpper();
}

QVariantList SysInfoCollector::collect() {
  QVariantList o;
  add(o, QObject::tr("System"), QObject::tr("Operating system"), QSysInfo::prettyProductName());
  add(o, QObject::tr("System"), QObject::tr("Architecture"), QSysInfo::currentCpuArchitecture());
  add(o, QObject::tr("System"), QObject::tr("Host name"), QSysInfo::machineHostName());
#if defined(Q_OS_MAC)
  collectMac(o);
#elif defined(Q_OS_WIN)
  collectWin(o);
#elif defined(Q_OS_LINUX)
  collectLinux(o);
#endif
  return o;
}
