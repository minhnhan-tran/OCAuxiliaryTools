#include "kextfetch.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>

#include "efibuilder.h"

namespace EfiBuilder {
namespace {

struct Http {
  const FetchOptions& opt;
  QNetworkAccessManager nam;
  explicit Http(const FetchOptions& o) : opt(o) {
    if (!o.proxyHost.isEmpty()) {
      QNetworkProxy p(QNetworkProxy::HttpProxy, o.proxyHost, quint16(o.proxyPort));
      nam.setProxy(p);
    }
  }
  // Synchronous GET that keeps the UI responsive. Empty result = failure.
  QByteArray get(const QUrl& url, QString* err) {
    QNetworkRequest rq(url);
    rq.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    rq.setRawHeader("User-Agent", "OCAuxiliaryTools-EFIBuilder");
    if (!opt.token.isEmpty() && url.host() == "api.github.com")
      rq.setRawHeader("Authorization", ("token " + opt.token).toUtf8());
    QNetworkReply* rep = nam.get(rq);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] { timedOut = true; loop.quit(); });
    timer.start(120000);
    loop.exec();
    QByteArray body;
    if (timedOut) {
      rep->abort();
      if (err) *err = "timed out";
    } else if (rep->error() != QNetworkReply::NoError) {
      if (err) *err = rep->errorString();
    } else {
      body = rep->readAll();
    }
    rep->deleteLater();
    return body;
  }
};

// Same name -> Dortania product mapping as OpCore-Simplify.
QString dortaniaProduct(const QString& n) {
  if (n.startsWith("AirportItlwm")) return QString();
  if (n.contains("VoodooPS2")) return "VoodooPS2";
  if (n == "BlueToolFixup" || n.startsWith("Brcm")) return "BrcmPatchRAM";
  if (n.startsWith("Ath3kBT")) return "Ath3kBT";
  if (n.startsWith("IntelB")) return "IntelBluetoothFirmware";
  if (n.startsWith("VoodooI2C")) return "VoodooI2C";
  if (n == "UTBDefault") return "USBToolBox";
  return n;
}

// AirportItlwm is built per macOS release.
QString itlwmOsName(int major) {
  switch (major) {
    case 17: return "High_Sierra";
    case 18: return "Mojave";
    case 19: return "Catalina";
    case 20: return "Big_Sur";
    case 21: return "Monterey";
    case 22: return "Ventura";
    case 23: return "Sonoma14.4";
    default: return QString();
  }
}

bool unzipTo(const QByteArray& zip, const QString& dir, const FetchOptions& opt) {
  const QString zf = dir + "/pkg.zip";
  QFile f(zf);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(zip);
  f.close();
  QDir().mkpath(dir + "/x");
  // unzip exits 1 for warnings only; the archive is still extracted.
  const int rc = QProcess::execute(opt.unzipProgram, QStringList() << "-qo" << zf << "-d" << dir + "/x");
  return rc == 0 || rc == 1;
}

// Shallowest "<name>.kext" directory in the extracted tree.
QString findKext(const QString& root, const QString& name) {
  QString best;
  QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString p = it.next();
    if (p.contains("__MACOSX") || !p.endsWith("/" + name + ".kext")) continue;
    if (best.isEmpty() || p.count('/') < best.count('/')) best = p;
  }
  return best;
}

bool copyTree(const QString& src, const QString& dst) {
  QDir().mkpath(dst);
  const QDir s(src);
  for (const QFileInfo& fi : s.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
    const QString to = dst + "/" + fi.fileName();
    if (fi.isDir() && !fi.isSymLink()) {
      if (!copyTree(fi.absoluteFilePath(), to)) return false;
    } else {
      QFile::remove(to);
      if (!QFile::copy(fi.absoluteFilePath(), to)) return false;
    }
  }
  return true;
}

QString pickAsset(const QJsonArray& assets, const QString& name, int major) {
  QStringList zips;
  for (const QJsonValue& a : assets) {
    const QString n = a.toObject()["name"].toString();
    const QString l = n.toLower();
    if (l.endsWith(".zip") && !l.contains("debug") && !l.contains("dsym") && !l.contains("source"))
      zips << n;
  }
  if (name.startsWith("AirportItlwm")) {
    const QString os = itlwmOsName(major);
    for (const QString& z : zips)
      if (!os.isEmpty() && z.startsWith("AirportItlwm-" + os + "-")) return z;
    return QString();
  }
  for (const QString& z : zips)
    if (z.startsWith(name + "-") || z.startsWith(name + "_")) return z;
  for (const QString& z : zips)
    if (z.contains("release", Qt::CaseInsensitive)) return z;
  return zips.value(0);
}

}  // namespace

FetchResult fetchKexts(const QStringList& names, int major, const QString& destDir,
                       const FetchOptions& opt, const FetchProgress& progress) {
  FetchResult res;
  QTemporaryDir tmp;
  if (!tmp.isValid()) {
    for (const QString& n : names) res.failed[n] = "no temporary directory";
    return res;
  }
  QDir().mkpath(destDir);
  Http http(opt);
  QJsonObject dortania;
  bool dortaniaTried = false;

  for (const QString& name : names) {
    if (progress && !progress(QString("Downloading %1...").arg(name))) {
      res.failed[name] = "cancelled";
      break;
    }
    const QJsonObject rec = kextRecord(name);
    const QString dir = tmp.path() + "/" + name;
    QDir().mkpath(dir);
    QStringList errs;
    QString found;

    // Tries one candidate URL; returns true once the kext was extracted.
    auto attempt = [&](const QUrl& url) -> bool {
      QString e;
      const QByteArray zip = http.get(url, &e);
      if (zip.isEmpty()) { errs << url.host() + ": " + e; return false; }
      QDir(dir + "/x").removeRecursively();
      if (!unzipTo(zip, dir, opt)) { errs << "unzip failed"; return false; }
      // "AirportItlwm-laobamac" is a catalog alias: the archive holds AirportItlwm.kext.
      found = findKext(dir + "/x", QString(name).remove("-laobamac"));
      if (found.isEmpty()) { errs << QString("%1.kext not in archive").arg(name); return false; }
      return true;
    };

    bool done = false;
    // 1) Dortania build repo
    const QString prod = dortaniaProduct(name);
    if (!prod.isEmpty()) {
      if (!dortaniaTried) {
        dortaniaTried = true;
        QString e;
        const QByteArray js = http.get(QUrl("https://raw.githubusercontent.com/dortania/build-repo/builds/latest.json"), &e);
        dortania = QJsonDocument::fromJson(js).object();
      }
      const QString link = dortania[prod].toObject()["versions"].toArray().at(0).toObject()["links"].toObject()["release"].toString();
      if (!link.isEmpty()) done = attempt(QUrl(link));
    }
    // 2) direct URL
    if (!done && !rec["url"].toString().isEmpty()) done = attempt(QUrl(rec["url"].toString()));
    // 3) GitHub releases
    if (!done && !rec["gh"].toString().isEmpty()) {
      QString e;
      const QString api = "https://api.github.com/repos/" + rec["gh"].toString() + "/releases";
      QByteArray js = http.get(QUrl(api + "/latest"), &e);
      QJsonObject rel;
      if (!js.isEmpty()) {
        rel = QJsonDocument::fromJson(js).object();
      } else {
        // Repos that only publish pre-releases (e.g. laobamac/itlwm) have no "latest": use the newest release.
        QString e2;
        js = http.get(QUrl(api + "?per_page=1"), &e2);
        rel = QJsonDocument::fromJson(js).array().first().toObject();
        if (rel.isEmpty()) e = e.isEmpty() ? e2 : e;
      }
      if (rel.isEmpty()) {
        errs << "github: " + e;
      } else {
        const QJsonArray assets = rel["assets"].toArray();
        const QString asset = pickAsset(assets, name, major);
        if (asset.isEmpty()) {
          errs << "no matching release asset" + (name.startsWith("AirportItlwm") ? QString(" for macOS (darwin %1)").arg(major) : QString());
        } else {
          for (const QJsonValue& a : assets)
            if (a.toObject()["name"].toString() == asset)
              done = attempt(QUrl(a.toObject()["browser_download_url"].toString()));
        }
      }
    }

    if (done && copyTree(found, destDir + "/" + name + ".kext")) res.ok << name;
    else res.failed[name] = errs.isEmpty() ? QString("could not install") : errs.join("; ");
    QCoreApplication::processEvents();
  }
  return res;
}

}  // namespace EfiBuilder
