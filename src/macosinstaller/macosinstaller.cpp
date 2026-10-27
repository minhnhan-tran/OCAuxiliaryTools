#include "macosinstaller.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QVersionNumber>

namespace MacInstaller {

QString shQuote(const QString& s) { QString q = s; q.replace("'", "'\\''"); return "'" + q + "'"; }

bool validVersion(const QString& v) {
  return QRegularExpression("\\A[0-9]+(\\.[0-9]+){0,2}\\z").match(v).hasMatch();
}
bool validDiskId(const QString& id) {
  return QRegularExpression("\\Adisk[0-9]+\\z").match(id).hasMatch();
}

QUrl catalogUrl() {
  return QUrl("https://swscan.apple.com/content/catalogs/others/index-26-15-14-13-12-10.16-10.15-10.14-10.13-10.12-10.11-10.10-10.9-mountainlion-lion-snowleopard-leopard.merged-1.sucatalog");
}

bool trustedAppleUrl(const QUrl& u) {
  const QString h = u.host().toLower();
  return u.isValid() && u.scheme() == "https" && (h == "apple.com" || h.endsWith(".apple.com")) && u.userInfo().isEmpty();
}

QList<Product> parseCatalog(const QVariantMap& root) {
  QList<Product> list;
  const QVariantMap products = root["Products"].toMap();
  for (auto it = products.begin(); it != products.end(); ++it) {
    const QVariantMap p = it.value().toMap();
    Product out;
    for (const QVariant& pv : p["Packages"].toList()) {
      const QVariantMap pk = pv.toMap();
      const QUrl u(pk["URL"].toString());
      if (u.path().endsWith("/InstallAssistant.pkg")) { out.pkgUrl = u; out.size = pk["Size"].toLongLong(); break; }
    }
    out.distUrl = QUrl(p["Distributions"].toMap()["English"].toString());
    if (out.pkgUrl.isEmpty() || out.size <= 0 || !trustedAppleUrl(out.pkgUrl) || !trustedAppleUrl(out.distUrl)) continue;
    out.id = it.key();
    out.date = p["PostDate"].toDateTime();
    list << out;
  }
  return list;
}

bool parseDistribution(const QString& xml, Product* p) {
  const QRegularExpression title("<title>([^<]+)</title>");
  auto key = [&](const char* k) {
    const QRegularExpressionMatch m = QRegularExpression(QString("<key>%1</key>\\s*<string>([^<]+)</string>").arg(k)).match(xml);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
  };
  const QRegularExpressionMatch t = title.match(xml);
  const QString v = key("VERSION"), b = key("BUILD");
  if (!t.hasMatch() || !validVersion(v) || !QRegularExpression("\\A[A-Za-z0-9]+\\z").match(b).hasMatch()) return false;
  p->title = t.captured(1).trimmed(); p->version = v; p->build = b;
  return true;
}

QString pkgFileName(const Product& p) {
  if (!validVersion(p.version) || !QRegularExpression("\\A[A-Za-z0-9]+\\z").match(p.build).hasMatch()) return QString();
  return "InstallAssistant-" + p.version + "-" + p.build + ".pkg";
}

bool packageSigned(const QString& out) {
  return out.contains("Status: signed Apple Software") || out.contains("Status: signed by a certificate trusted by macOS");
}

QString installPkgCommand(const QString& pkg) {
  if (!pkg.endsWith(".pkg")) return QString();
  return "sudo installer -pkg " + shQuote(pkg) + " -target /";
}

bool isBetaBuild(const QString& build) {
  // Apple has no beta flag in the catalog. Seed builds end in a lowercase letter (25A5316i) or
  // carry a build number of 5000+ (26B7124), while release builds stay lower (25G332, 21H1320).
  if (QRegularExpression("[0-9][a-z]\\z").match(build).hasMatch()) return true;
  const QRegularExpressionMatch m = QRegularExpression("\\A[0-9]+[A-Z]+([0-9]+)\\z").match(build);
  return m.hasMatch() && m.captured(1).toInt() >= 5000;
}

bool newerFirst(const Product& a, const Product& b) {
  const QVersionNumber va = QVersionNumber::fromString(a.version), vb = QVersionNumber::fromString(b.version);
  if (va != vb) return va > vb;
  if (a.date != b.date) return a.date > b.date;
  return a.build > b.build;
}

QString humanSize(qint64 b) {
  if (b < 1000000000LL) return QString::number(b / 1e6, 'f', 1) + " MB";
  return QString::number(b / 1e9, 'f', b >= 100e9 ? 0 : 1) + " GB";
}

static QString wholeDisk(QString id) {  // disk0s2 -> disk0
  const QRegularExpressionMatch m = QRegularExpression("\\A(disk[0-9]+)").match(id);
  return m.hasMatch() ? m.captured(1) : QString();
}

QStringList bootDiskIds(const QJsonObject& info) {
  QStringList ids;
  auto add = [&](const QString& s) { const QString w = wholeDisk(s); if (!w.isEmpty() && !ids.contains(w)) ids << w; };
  add(info["ParentWholeDisk"].toString());
  add(info["DeviceIdentifier"].toString());
  for (const QJsonValue& v : info["APFSPhysicalStores"].toArray())
    add(v.toObject()["APFSPhysicalStore"].toString());
  return ids;
}

bool eligibleDisk(const QJsonObject& info, const QStringList& bootDisks, Disk* out, QString* why) {
  auto no = [&](const QString& r) { if (why) *why = r; return false; };
  const QString node = info["DeviceNode"].toString();
  const QString id = node.startsWith("/dev/") ? node.mid(5) : QString();
  if (!validDiskId(id)) return no("not a whole disk");
  if (!info["WholeDisk"].toBool()) return no("not a whole disk");
  if (info["Internal"].toBool()) return no("internal disk");
  if (info["VirtualOrPhysical"].toString() == "Virtual") return no("virtual disk");
  if (!info["RemovableMediaOrExternalDevice"].toBool()) return no("not external");
  if (!info["Writable"].toBool(true)) return no("read-only");
  if (bootDisks.contains(id)) return no("holds the running system");
  const qint64 size = qint64(info["TotalSize"].toDouble());
  if (size < kMinUsbBytes) return no(QString("too small (%1 GB, needs 16 GB)").arg(size / 1000000000));
  if (out) {
    out->id = id; out->size = size; out->bus = info["BusProtocol"].toString();
    out->name = info["MediaName"].toString().trimmed();
  }
  return true;
}

QString checkEfiFolder(const QString& dir) {
  if (dir.isEmpty()) return "no folder chosen";
  const QDir d(dir);
  if (!d.exists()) return "the folder does not exist";
  if (!QFileInfo(d.filePath("BOOT/BOOTx64.efi")).isFile()) return "BOOT/BOOTx64.efi is missing (pick the EFI folder itself)";
  if (!QFileInfo(d.filePath("OC/config.plist")).isFile()) return "OC/config.plist is missing (pick the EFI folder itself)";
  return QString();
}

QString volumeLabel(const Disk& disk) { return "OCAT-" + disk.id; }

QString createCommand(const QString& app, const Disk& disk, const QString& efi) {
  const QString cim = app + "/Contents/Resources/createinstallmedia";
  if (!validDiskId(disk.id) || !QFileInfo(app).fileName().startsWith("Install ") || !app.endsWith(".app")) return QString();
  const QString dev = "/dev/" + disk.id, label = volumeLabel(disk), vol = "/Volumes/" + label;
  QString c = "sudo diskutil eraseDisk JHFS+ " + label + " GPT " + dev +
              " && sudo " + shQuote(cim) + " --volume " + shQuote(vol) + " --nointeraction";
  if (!efi.isEmpty())
    c += " && diskutil mount " + dev + "s1 >/dev/null && mp=$(diskutil info -plist " + dev +
         "s1 | plutil -extract MountPoint raw -o - -) && [ -n \"$mp\" ] && sudo ditto " + shQuote(efi) + " \"$mp/EFI\"";
  return c;
}

#ifdef Q_OS_MAC
static QJsonObject runJson(const QString& prog, const QStringList& args, QString* err) {
  QProcess p;
  p.start(prog, args);
  if (!p.waitForFinished(15000) || p.exitCode() != 0) {
    if (err) *err = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return QJsonObject();
  }
  QProcess pl;
  pl.start("plutil", QStringList() << "-convert" << "json" << "-o" << "-" << "-");
  pl.write(p.readAllStandardOutput());
  pl.closeWriteChannel();
  if (!pl.waitForFinished(10000) || pl.exitCode() != 0) { if (err) *err = "plutil failed"; return QJsonObject(); }
  return QJsonDocument::fromJson(pl.readAllStandardOutput()).object();
}

QList<Installer> findInstallers(const QString& dir) {
  QList<Installer> list;
  const QFileInfoList apps = QDir(dir).entryInfoList(QStringList() << "Install macOS*.app" << "Install OS X*.app", QDir::Dirs, QDir::Name);
  for (const QFileInfo& fi : apps) {
    if (!QFileInfo(fi.filePath() + "/Contents/Resources/createinstallmedia").isExecutable()) continue;
    Installer in; in.path = fi.filePath(); in.name = fi.completeBaseName();
    QProcess p;
    p.start("plutil", QStringList() << "-extract" << "CFBundleShortVersionString" << "raw" << "-o" << "-" << (fi.filePath() + "/Contents/Info.plist"));
    if (p.waitForFinished(5000) && p.exitCode() == 0) in.version = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    list << in;
  }
  return list;
}

QList<Disk> listExternalDisks(QStringList* skipped, QString* error) {
  QList<Disk> disks;
  QString err;
  const QJsonObject root = runJson("diskutil", QStringList() << "info" << "-plist" << "/", &err);
  QStringList boot = bootDiskIds(root);
  // `/` is a volume of an APFS container; the physical disk(s) are only listed on the container.
  for (const QString& id : QStringList(boot))
    for (const QString& extra : bootDiskIds(runJson("diskutil", QStringList() << "info" << "-plist" << id, &err)))
      if (!boot.contains(extra)) boot << extra;
  const QJsonObject ls = runJson("diskutil", QStringList() << "list" << "-plist" << "external" << "physical", &err);
  if (ls.isEmpty() && !err.isEmpty()) { if (error) *error = err; return disks; }
  for (const QJsonValue& v : ls["WholeDisks"].toArray()) {
    const QString id = v.toString();
    if (!validDiskId(id)) continue;
    const QJsonObject info = runJson("diskutil", QStringList() << "info" << "-plist" << id, &err);
    Disk d; QString why;
    if (eligibleDisk(info, boot, &d, &why)) disks << d;
    else if (skipped) *skipped << id + ": " + why;
  }
  return disks;
}
#endif

}  // namespace MacInstaller
