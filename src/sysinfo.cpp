#include "sysinfo.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
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

// "%03%08%00%00" (nvram's escaped bytes) -> "03080000"
QString nvramBytesToHex(const QString& s) {
  QString hex;
  const QStringList parts = s.split('%', QString::SkipEmptyParts);
  for (const QString& p : parts) hex += p.left(2);
  return hex.toUpper();
}

#ifdef Q_OS_MAC
void collectMac(QVariantList& o) {
  const QString sys = QObject::tr("System"), cpu = QObject::tr("Processor"), mem = QObject::tr("Memory"),
                gpu = QObject::tr("Graphics"), oc = QObject::tr("Boot");
  add(o, sys, QObject::tr("Model identifier"), run("sysctl", {"-n", "hw.model"}));
  add(o, sys, QObject::tr("macOS"), run("sw_vers", {"-productName"}) + " " + run("sw_vers", {"-productVersion"}) +
                                       " (" + run("sw_vers", {"-buildVersion"}) + ")");
  add(o, sys, QObject::tr("Kernel"), QSysInfo::kernelVersion());

  const QJsonObject hw = QJsonDocument::fromJson(run("system_profiler", {"SPHardwareDataType", "-json"}, 15000).toUtf8())
                             .object();
  const QJsonObject h = hw["SPHardwareDataType"].toArray().first().toObject();
  add(o, sys, QObject::tr("Serial number"), h["serial_number"].toString());
  add(o, sys, QObject::tr("Board ID"), run("sh", {"-c", "ioreg -d2 -c IOPlatformExpertDevice | awk -F'\"' '/board-id/{print $4}'"}));
  add(o, sys, QObject::tr("Firmware"), h["boot_rom_version"].toString());

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

  const QJsonObject gd = QJsonDocument::fromJson(run("system_profiler", {"SPDisplaysDataType", "-json"}, 20000).toUtf8()).object();
  int n = 0;
  for (const QJsonValue& v : gd["SPDisplaysDataType"].toArray()) {
    const QJsonObject g = v.toObject();
    const QString label = QObject::tr("GPU %1").arg(++n);
    QString name = g["sppci_model"].toString();
    QString ids;
    const QString vid = g["spdisplays_vendor-id"].toString(), did = g["spdisplays_device-id"].toString();
    if (!did.isEmpty()) ids = (vid.isEmpty() ? QString() : vid + ":") + did;
    QString vram = g["spdisplays_vram"].toString();
    if (vram.isEmpty()) vram = g["spdisplays_vram_shared"].toString();
    QStringList parts{name};
    if (!ids.isEmpty()) parts << "[" + ids + "]";
    if (!vram.isEmpty()) parts << "- " + vram;
    add(o, gpu, label, parts.join(' '));
  }

  const QString ocVer = run("nvram", {"4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:opencore-version"});
  if (ocVer.contains('\t')) add(o, oc, QObject::tr("OpenCore"), ocVer.section('\t', 1).trimmed());
  else add(o, oc, QObject::tr("OpenCore"), QObject::tr("not detected (no opencore-version in NVRAM)"));
  const QString sip = run("csrutil", {"status"});
  // csrutil prints a multi-line report when SIP is partly off; the first line is the verdict.
  add(o, oc, QObject::tr("SIP"), sip.section('\n', 0, 0).section(':', 1).trimmed().remove(QRegularExpression("\\.$")));
  const QString csr = run("nvram", {"csr-active-config"});
  if (csr.contains('\t')) add(o, oc, QObject::tr("csr-active-config"), nvramBytesToHex(csr.section('\t', 1).trimmed()));
  const QString bootArgs = run("nvram", {"boot-args"});
  if (bootArgs.contains('\t')) add(o, oc, QObject::tr("boot-args"), bootArgs.section('\t', 1).trimmed());
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
