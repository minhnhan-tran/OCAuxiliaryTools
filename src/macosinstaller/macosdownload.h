#ifndef MACOSDOWNLOAD_H
#define MACOSDOWNLOAD_H

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QTimer>

#include "macosinstaller.h"

namespace MacInstaller {

// Reads Apple's software-update catalog and each installer's distribution file (for the
// name, version and build), like OpenCore Legacy Patcher does. Only apple.com over https.
class CatalogLoader : public QObject {
  Q_OBJECT
 public:
  explicit CatalogLoader(QNetworkAccessManager* nam, QObject* parent = nullptr);
  void start();
  void cancel();
 signals:
  void finished(const QList<MacInstaller::Product>& products, const QString& error);

 private:
  void gotCatalog();
  void gotDistribution(QNetworkReply* r, int index);
  void maybeDone();
  QNetworkAccessManager* nam;
  QNetworkReply* catalogReply = nullptr;
  QList<QNetworkReply*> distReplies;
  QList<Product> products, ready;
  int pending = 0;
  bool cancelled = false;
};

// Resumable HTTPS download into "<path>.part", renamed to <path> only once the size matches.
// A stalled connection (no bytes for 60 s) is aborted; the partial file is kept for resume.
class Downloader : public QObject {
  Q_OBJECT
 public:
  explicit Downloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
  bool start(const QUrl& url, const QString& path, qint64 expectedSize, QString* error);
  void cancel();
  bool running() const { return reply != nullptr; }
 signals:
  void progress(qint64 done, qint64 total);
  void finished(bool ok, const QString& error);

 private:
  void onReadyRead();
  void onFinished();
  void fail(const QString& msg);
  QNetworkAccessManager* nam;
  QNetworkReply* reply = nullptr;
  QFile part;
  QString finalPath;
  qint64 expected = 0, offset = 0, received = 0;
  bool checkedStatus = false, userCancel = false, stalled = false;
  QTimer watchdog;
  qint64 lastProgress = -1;
};

}  // namespace MacInstaller

#endif
