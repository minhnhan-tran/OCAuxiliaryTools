#ifndef SELFUPDATE_H
#define SELFUPDATE_H

// In-place update from a GitHub release (macOS).
//
// The app downloads the release's .dmg, checks it against the SHA-256 that GitHub publishes for the asset,
// mounts it, checks the app inside (same bundle id, announced version, valid code signature, same signing
// team as the running app), copies it NEXT TO the running app, and lets a small script swap the two once this
// process has quit. Any failed check leaves the installed app untouched.

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

class QProgressDialog;
class QWidget;

namespace SelfUpdate {

// owner/name on github.com that releases are read from.
extern const char* const kRepo;

struct Release {
  QString version;  // "1.36.0": the tag without its leading "v"
  QString url;      // https download URL of the installable disk image
  QString name;     // asset file name
  QString sha256;   // lower-case hex, taken from the asset digest GitHub publishes
  qint64 size = 0;  // bytes; 0 when unknown
  QString notes;

  bool valid() const { return !version.isEmpty() && url.startsWith("https://"); }
  // Without a published digest the download cannot be verified, so it is never installed in place.
  bool verifiable() const { return sha256.size() == 64; }
};

// "sha256:ABCD..." -> "abcd..."; empty for any other algorithm or malformed value.
QString digestHex(const QString& digest);

// Fills `rel` from one asset object of the GitHub releases API (name, browser_download_url, size, digest).
void fillFromAsset(const QVariantMap& asset, Release* rel);

// Path of the .app bundle this process runs from; empty when it is not an app bundle.
QString bundlePath();

// Empty when `bundle` can be replaced in place, otherwise why not (read-only volume, translocated, ...).
QString unsupportedReason(const QString& bundle);

bool sha256File(const QString& path, QString* hex);

// Blocking. Verifies `dmgPath` and the app inside it, then copies that app next to `bundle` and returns the
// copy in `stagedApp`. On failure nothing is left behind and `err` says why.
bool stage(const QString& dmgPath, const Release& rel, const QString& bundle, qint64 pid, QString* stagedApp,
           QString* err);

// Starts the detached script that waits for process `pid` to exit and then swaps `stagedApp` in for `bundle`
// (rolling back if anything fails) and reopens the app. Call right before quitting.
bool launchSwap(const QString& bundle, const QString& stagedApp, qint64 pid, QString* err);

// Removes a staged copy that will not be installed.
void discard(const QString& stagedApp);

// Deletes leftovers of earlier updates (hidden ".<name>.app.update-<pid>" / ".old-<pid>" next to the app and
// temp download folders) whose process is gone. Call once at startup.
void cleanupStale();

}  // namespace SelfUpdate

// Download + verify + stage with a progress dialog, then offers to restart.
class SelfUpdater : public QObject {
  Q_OBJECT

 public:
  explicit SelfUpdater(QWidget* parent);
  void start(const SelfUpdate::Release& rel);

 private slots:
  void onReadyRead();
  void onProgress(qint64 got, qint64 total);
  void onDownloaded();
  void cancel();
  void onStalled();

 private:
  void fail(const QString& message, bool offerPage = true);
  void finish();
  void staged();

  QWidget* parent_;
  SelfUpdate::Release rel_;
  QString bundle_, dir_, dmg_, stagedApp_, stageError_;
  QNetworkAccessManager nam_;
  QNetworkReply* reply_ = nullptr;
  QFile file_;
  QProgressDialog* progress_ = nullptr;
  QTimer stall_;
  bool canceled_ = false;
  bool writeFailed_ = false;
};

#endif  // SELFUPDATE_H
