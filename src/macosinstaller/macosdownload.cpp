#include "macosdownload.h"

#include <QBuffer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtAlgorithms>

#include "../plistparser.h"

namespace MacInstaller {

CatalogLoader::CatalogLoader(QNetworkAccessManager* n, QObject* parent) : QObject(parent), nam(n) {}

void CatalogLoader::cancel() {
  cancelled = true;
  if (catalogReply) catalogReply->abort();
  for (QNetworkReply* r : distReplies) if (r) r->abort();
}

void CatalogLoader::start() {
  cancelled = false;
  QNetworkRequest rq(catalogUrl());
  rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  catalogReply = nam->get(rq);
  connect(catalogReply, &QNetworkReply::finished, this, &CatalogLoader::gotCatalog);
}

void CatalogLoader::gotCatalog() {
  QNetworkReply* r = catalogReply;
  catalogReply = nullptr;
  r->deleteLater();
  if (cancelled) return;
  if (r->error() != QNetworkReply::NoError) {
    emit finished({}, tr("Could not download Apple's installer catalog: %1").arg(r->errorString()));
    return;
  }
  QByteArray data = r->readAll();
  QBuffer buf(&data);
  buf.open(QIODevice::ReadOnly);
  const QVariantMap root = PListParser::parsePList(&buf).toMap();
  products = parseCatalog(root);
  if (products.isEmpty()) {
    emit finished({}, tr("Apple's catalog has no macOS installers (or its format changed)."));
    return;
  }
  pending = products.size();
  for (int i = 0; i < products.size(); ++i) {
    QNetworkRequest rq(products[i].distUrl);
    rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* dr = nam->get(rq);
    distReplies << dr;
    connect(dr, &QNetworkReply::finished, this, [this, dr, i] { gotDistribution(dr, i); });
  }
}

void CatalogLoader::gotDistribution(QNetworkReply* r, int index) {
  r->deleteLater();
  if (!cancelled && r->error() == QNetworkReply::NoError) {
    Product p = products[index];
    if (parseDistribution(QString::fromUtf8(r->read(1 << 20)), &p)) ready << p;
  }
  --pending;
  maybeDone();
}

void CatalogLoader::maybeDone() {
  if (pending > 0 || cancelled) return;
  distReplies.clear();
  QList<Product> out;
  for (const Product& p : ready) {  // the merged catalog can repeat a build
    bool dup = false;
    for (const Product& o : out) if (o.version == p.version && o.build == p.build) dup = true;
    if (!dup) out << p;
  }
  ready.clear();
  std::sort(out.begin(), out.end(), newerFirst);
  emit finished(out, out.isEmpty() ? tr("Could not read the installer details from Apple.") : QString());
}

// ---------------------------------------------------------------------------------------------

Downloader::Downloader(QNetworkAccessManager* n, QObject* parent) : QObject(parent), nam(n) {
  watchdog.setInterval(60000);
  connect(&watchdog, &QTimer::timeout, this, [this] {
    const qint64 now = offset + received;
    if (now != lastProgress) { lastProgress = now; return; }
    stalled = true;
    if (reply) reply->abort();
  });
}

bool Downloader::start(const QUrl& url, const QString& path, qint64 expectedSize, QString* error) {
  auto no = [&](const QString& m) { if (error) *error = m; return false; };
  if (reply) return no(tr("A download is already running."));
  if (!trustedAppleUrl(url)) return no(tr("Refusing to download from a non-Apple address."));
  if (expectedSize <= 0) return no(tr("Unknown download size."));
  finalPath = path;
  expected = expectedSize;
  userCancel = stalled = checkedStatus = false;
  received = 0;
  part.setFileName(path + ".part");
  offset = QFileInfo(part).exists() ? QFileInfo(part).size() : 0;
  if (offset > expected) { QFile::remove(part.fileName()); offset = 0; }
  if (offset == expected) {  // a complete earlier download that was never renamed
    if (!QFile::rename(part.fileName(), finalPath)) return no(tr("Could not move the finished download into place."));
    emit progress(expected, expected);
    emit finished(true, QString());
    return true;
  }
  if (!part.open(offset ? QIODevice::Append : QIODevice::WriteOnly)) return no(tr("Cannot write %1: %2").arg(part.fileName(), part.errorString()));
  QNetworkRequest rq(url);
  rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  if (offset) rq.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");
  reply = nam->get(rq);
  connect(reply, &QNetworkReply::readyRead, this, &Downloader::onReadyRead);
  connect(reply, &QNetworkReply::finished, this, &Downloader::onFinished);
  lastProgress = -1;
  watchdog.start();
  emit progress(offset, expected);
  return true;
}

void Downloader::cancel() {
  userCancel = true;
  if (reply) reply->abort();
}

void Downloader::onReadyRead() {
  if (!reply) return;
  if (!checkedStatus) {
    checkedStatus = true;
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (offset > 0 && code == 200) {  // server ignored Range: start over
      part.close();
      part.open(QIODevice::WriteOnly | QIODevice::Truncate);
      offset = 0;
    } else if (code >= 400) {
      return;  // reported by onFinished()
    }
  }
  const QByteArray chunk = reply->readAll();
  if (part.write(chunk) != chunk.size()) {
    fail(tr("Writing to disk failed: %1").arg(part.errorString()));
    return;
  }
  received += chunk.size();
  emit progress(offset + received, expected);
}

void Downloader::fail(const QString& msg) {
  QNetworkReply* r = reply;
  reply = nullptr;
  watchdog.stop();
  part.close();
  if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
  emit finished(false, msg);
}

void Downloader::onFinished() {
  if (!reply) return;
  QNetworkReply* r = reply;
  reply = nullptr;
  watchdog.stop();
  r->deleteLater();
  if (r->bytesAvailable() > 0 && r->error() == QNetworkReply::NoError) {
    const QByteArray rest = r->readAll();
    part.write(rest);
    received += rest.size();
  }
  part.close();
  const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (userCancel) { emit finished(false, tr("Cancelled. The partial download is kept so it can be resumed.")); return; }
  if (stalled) { emit finished(false, tr("The connection stalled. Press Download to resume.")); return; }
  if (code == 416 && QFileInfo(part).size() >= expected) {
    // fall through to the size check below
  } else if (r->error() != QNetworkReply::NoError) {
    emit finished(false, tr("Download failed: %1 (press Download to resume)").arg(r->errorString()));
    return;
  }
  const qint64 size = QFileInfo(part).size();
  if (size != expected) {
    emit finished(false, tr("The download is incomplete (%1 of %2). Press Download to resume.").arg(humanSize(size), humanSize(expected)));
    return;
  }
  QFile::remove(finalPath);
  if (!QFile::rename(part.fileName(), finalPath)) { emit finished(false, tr("Could not move the finished download into place.")); return; }
  emit progress(expected, expected);
  emit finished(true, QString());
}

}  // namespace MacInstaller
