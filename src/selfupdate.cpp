#include "selfupdate.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUrl>
#include <QDesktopServices>
#include <QPushButton>
#include <QThread>
#include <climits>

#ifdef Q_OS_MAC
#include <signal.h>
#endif

extern QString CurVersion;

namespace SelfUpdate {

const char* const kRepo = "minhnhan-tran/OCAuxiliaryTools";

QString digestHex(const QString& digest) {
  const QString d = digest.trimmed().toLower();
  if (!d.startsWith("sha256:")) return QString();
  const QString hex = d.mid(7);
  static const QRegularExpression re("^[0-9a-f]{64}$");
  return re.match(hex).hasMatch() ? hex : QString();
}

void fillFromAsset(const QVariantMap& asset, Release* rel) {
  rel->name = QFileInfo(asset.value("name").toString()).fileName();  // never a path
  rel->url = asset.value("browser_download_url").toString();
  rel->size = asset.value("size").toLongLong();
  rel->sha256 = digestHex(asset.value("digest").toString());
}

bool sha256File(const QString& path, QString* hex) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  QCryptographicHash h(QCryptographicHash::Sha256);
  while (!f.atEnd()) {
    const QByteArray chunk = f.read(1 << 20);
    if (chunk.isEmpty() && f.error() != QFile::NoError) return false;
    h.addData(chunk);
  }
  *hex = QString::fromLatin1(h.result().toHex());
  return true;
}

QString bundlePath() {
#ifdef Q_OS_MAC
  // <name>.app/Contents/MacOS/<exe>
  QDir d(QCoreApplication::applicationDirPath());
  if (!d.cdUp() || !d.cdUp()) return QString();
  const QString p = d.absolutePath();
  if (!p.endsWith(".app") || !QFileInfo(p + "/Contents/Info.plist").exists()) return QString();
  return p;
#else
  return QString();
#endif
}

QString unsupportedReason(const QString& bundle) {
#ifdef Q_OS_MAC
  if (bundle.isEmpty()) return QObject::tr("This copy is not running from an application bundle.");
  if (bundle.contains("/AppTranslocation/"))
    return QObject::tr("The app is running from a temporary location. Move it to Applications and open it again.");
  const QFileInfo parent(QFileInfo(bundle).absolutePath());
  if (!parent.isWritable())
    return QObject::tr("The folder %1 is not writable, so the app cannot replace itself.").arg(parent.absoluteFilePath());
  return QString();
#else
  Q_UNUSED(bundle);
  return QObject::tr("In-place update is only available on macOS.");
#endif
}

#ifdef Q_OS_MAC
namespace {

// Runs a program to completion; returns its exit code, or -1 when it did not start / time out.
int run(const QString& prog, const QStringList& args, QString* out = nullptr, int timeoutMs = 120000) {
  QProcess p;
  p.setProcessChannelMode(QProcess::MergedChannels);
  p.start(prog, args);
  if (!p.waitForStarted(5000)) return -1;
  if (!p.waitForFinished(timeoutMs)) {
    p.kill();
    p.waitForFinished(2000);
    return -1;
  }
  if (out) *out = QString::fromUtf8(p.readAll());
  return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

QString plistValue(const QString& app, const QString& key) {
  QString out;
  if (run("/usr/libexec/PlistBuddy", {"-c", "Print :" + key, app + "/Contents/Info.plist"}, &out, 15000) != 0)
    return QString();
  return out.trimmed();
}

// Signing team of a bundle; "" for unsigned and ad-hoc signatures alike (neither names a publisher).
QString teamOf(const QString& app) {
  QString out;
  if (run("/usr/bin/codesign", {"-dvv", app}, &out, 30000) != 0) return QString();
  QRegularExpressionMatch m = QRegularExpression("TeamIdentifier=(\\S+)").match(out);
  const QString t = m.hasMatch() ? m.captured(1) : QString();
  return t == "not" ? QString() : t;  // "TeamIdentifier=not set"
}

bool signatureValid(const QString& app, QString* why) {
  QString out;
  if (run("/usr/bin/codesign", {"--verify", "--deep", "--strict", app}, &out, 120000) == 0) return true;
  *why = out.trimmed().left(300);
  return false;
}

QString hiddenSibling(const QString& bundle, const QString& tag, qint64 pid) {
  const QFileInfo fi(bundle);
  return fi.absolutePath() + "/." + fi.fileName() + "." + tag + "-" + QString::number(pid);
}

void detach(const QString& mnt) {
  if (run("/usr/bin/hdiutil", {"detach", mnt, "-quiet"}, nullptr, 60000) != 0)
    run("/usr/bin/hdiutil", {"detach", mnt, "-force", "-quiet"}, nullptr, 60000);
}

}  // namespace
#endif

bool stage(const QString& dmgPath, const Release& rel, const QString& bundle, qint64 pid, QString* stagedApp,
           QString* err) {
#ifdef Q_OS_MAC
  stagedApp->clear();
  if (!rel.verifiable()) {
    *err = QObject::tr("The release publishes no checksum for this file, so it cannot be verified.");
    return false;
  }
  if (rel.size > 0 && QFileInfo(dmgPath).size() != rel.size) {
    *err = QObject::tr("The download is incomplete or the wrong size.");
    return false;
  }
  QString hex;
  if (!sha256File(dmgPath, &hex)) {
    *err = QObject::tr("The downloaded file could not be read.");
    return false;
  }
  if (hex != rel.sha256) {
    *err = QObject::tr("The checksum of the download does not match the one published on GitHub.");
    return false;
  }

  const QString work = QFileInfo(dmgPath).absolutePath();
  const QString mnt = work + "/mnt";
  QDir().mkpath(mnt);
  QString out;
  if (run("/usr/bin/hdiutil", {"attach", dmgPath, "-nobrowse", "-readonly", "-noautoopen", "-mountpoint", mnt}, &out) !=
      0) {
    *err = QObject::tr("The disk image could not be opened.") + "\n" + out.trimmed().left(200);
    return false;
  }

  const QString copy = hiddenSibling(bundle, "update", pid);
  auto fail = [&](const QString& msg) {
    detach(mnt);
    QDir(copy).removeRecursively();
    *err = msg;
    return false;
  };

  // exactly one app in the image
  const QStringList apps = QDir(mnt).entryList({"*.app"}, QDir::Dirs | QDir::NoDotAndDotDot);
  if (apps.size() != 1) return fail(QObject::tr("The disk image does not contain exactly one application."));
  const QString src = mnt + "/" + apps.first();

  const QString runId = plistValue(bundle, "CFBundleIdentifier");
  if (runId.isEmpty() || plistValue(src, "CFBundleIdentifier") != runId)
    return fail(QObject::tr("The application in the download is not this application."));
  const QString ver = plistValue(src, "CFBundleShortVersionString");
  if (ver != rel.version)
    return fail(QObject::tr("The application in the download is version %1, not the announced %2.")
                    .arg(ver.isEmpty() ? QObject::tr("unknown") : ver, rel.version));
  const QString exe = plistValue(src, "CFBundleExecutable");
  if (exe.isEmpty() || !QFileInfo(src + "/Contents/MacOS/" + exe).isExecutable())
    return fail(QObject::tr("The application in the download has no executable."));

  QString why;
  if (!signatureValid(src, &why)) return fail(QObject::tr("The code signature of the download is not valid.") + "\n" + why);
  if (teamOf(src) != teamOf(bundle))
    return fail(QObject::tr("The download is signed by a different developer than the installed app."));

  // Copy next to the installed app (same volume, so the later swap is a rename), then check the copy itself.
  QDir(copy).removeRecursively();
  if (run("/usr/bin/ditto", {src, copy}, &out) != 0)
    return fail(QObject::tr("The update could not be copied next to the app (disk full?).") + "\n" + out.trimmed().left(200));
  run("/usr/bin/xattr", {"-cr", copy}, nullptr, 60000);
  if (!signatureValid(copy, &why)) return fail(QObject::tr("The copied update failed its signature check.") + "\n" + why);

  detach(mnt);
  *stagedApp = copy;
  return true;
#else
  Q_UNUSED(dmgPath);
  Q_UNUSED(rel);
  Q_UNUSED(bundle);
  Q_UNUSED(pid);
  stagedApp->clear();
  *err = QObject::tr("In-place update is only available on macOS.");
  return false;
#endif
}

bool launchSwap(const QString& bundle, const QString& stagedApp, qint64 pid, QString* err) {
#ifdef Q_OS_MAC
  const QString script = QDir::tempPath() + "/ocat-swap-" + QString::number(pid) + ".sh";
  QFile f(script);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    *err = QObject::tr("The update script could not be written.");
    return false;
  }
  // Waits for the running app to exit; if it never does (the user cancelled the quit), nothing is touched.
  // The old app is moved aside, the new one renamed into place, and the old one is put back if that fails.
  f.write(
      "#!/bin/sh\n"
      "OLD=\"$1\"; NEW=\"$2\"; PID=\"$3\"\n"
      "BAK=\"$(dirname \"$OLD\")/.$(basename \"$OLD\").old-$PID\"\n"
      "n=0; while kill -0 \"$PID\" 2>/dev/null && [ $n -lt 150 ]; do sleep 0.2; n=$((n+1)); done\n"
      "if kill -0 \"$PID\" 2>/dev/null; then rm -rf \"$NEW\"; rm -f \"$0\"; exit 0; fi\n"
      "if mv \"$OLD\" \"$BAK\"; then\n"
      "  if mv \"$NEW\" \"$OLD\"; then rm -rf \"$BAK\"; else mv \"$BAK\" \"$OLD\"; rm -rf \"$NEW\"; fi\n"
      "else\n"
      "  rm -rf \"$NEW\"\n"
      "fi\n"
      "open \"$OLD\"\n"
      "rm -f \"$0\"\n");
  f.close();
  if (!QProcess::startDetached("/bin/sh", {script, bundle, stagedApp, QString::number(pid)})) {
    *err = QObject::tr("The update script could not be started.");
    return false;
  }
  return true;
#else
  Q_UNUSED(bundle);
  Q_UNUSED(stagedApp);
  Q_UNUSED(pid);
  *err = QObject::tr("In-place update is only available on macOS.");
  return false;
#endif
}

void discard(const QString& stagedApp) {
  if (!stagedApp.isEmpty()) QDir(stagedApp).removeRecursively();
}

void cleanupStale() {
#ifdef Q_OS_MAC
  const QString bundle = bundlePath();
  if (bundle.isEmpty()) return;
  const QFileInfo fi(bundle);
  const QDir parent(fi.absolutePath());
  const QString prefix = "." + fi.fileName() + ".";
  for (const QString& n : parent.entryList(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
    if (!n.startsWith(prefix)) continue;
    const QString tag = n.mid(prefix.size());  // "update-123" / "old-123"
    const qint64 pid = tag.section('-', 1).toLongLong();
    if (!(tag.startsWith("update-") || tag.startsWith("old-")) || pid <= 0) continue;
    if (pid == QCoreApplication::applicationPid() || kill(static_cast<pid_t>(pid), 0) == 0) continue;  // still in use
    QDir(parent.absoluteFilePath(n)).removeRecursively();
  }
  // Download folders of earlier runs: only those whose process is gone (another OCAT may be mid-download).
  for (const QString& d : QDir(QDir::tempPath()).entryList({"ocat-update-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
    const qint64 pid = d.mid(QString("ocat-update-").size()).toLongLong();
    if (pid <= 0 || pid == QCoreApplication::applicationPid() || kill(static_cast<pid_t>(pid), 0) == 0) continue;
    const QString dir = QDir::tempPath() + "/" + d;
    // a crash can leave the image mounted; never delete through a live mount
    if (QFileInfo(dir + "/mnt").isDir()) detach(dir + "/mnt");
    QDir(dir).removeRecursively();
  }
#endif
}

}  // namespace SelfUpdate

// ------------------------------------------------------------------------------------------------

namespace {
// Verification mounts an image and runs codesign: seconds of blocking work, so it runs off the GUI thread.
// No Q_OBJECT needed; the result is read in the finished() handler, after run() has returned.
class StageThread : public QThread {
 public:
  StageThread(const SelfUpdate::Release& rel, const QString& dmg, const QString& bundle, qint64 pid, QObject* parent)
      : QThread(parent), rel_(rel), dmg_(dmg), bundle_(bundle), pid_(pid) {}
  bool ok = false;
  QString app, err;

 protected:
  void run() override { ok = SelfUpdate::stage(dmg_, rel_, bundle_, pid_, &app, &err); }

 private:
  SelfUpdate::Release rel_;
  QString dmg_, bundle_;
  qint64 pid_;
};
}  // namespace

SelfUpdater::SelfUpdater(QWidget* parent) : QObject(parent), parent_(parent) {
  stall_.setSingleShot(true);
  connect(&stall_, &QTimer::timeout, this, &SelfUpdater::onStalled);
}

void SelfUpdater::fail(const QString& message, bool offerPage) {
  if (progress_) {
    progress_->close();
    progress_->deleteLater();
    progress_ = nullptr;
  }
  stall_.stop();
  if (!dir_.isEmpty()) QDir(dir_).removeRecursively();
  QMessageBox box(QMessageBox::Warning, tr("Update"), tr("The update could not be installed.") + "\n\n" + message,
                  QMessageBox::Ok, parent_);
  QPushButton* page = offerPage ? box.addButton(tr("Open release page"), QMessageBox::ActionRole) : nullptr;
  box.exec();
  if (page && box.clickedButton() == page)
    QDesktopServices::openUrl(QUrl(QString("https://github.com/%1/releases/latest").arg(SelfUpdate::kRepo)));
  deleteLater();
}

void SelfUpdater::start(const SelfUpdate::Release& rel) {
  rel_ = rel;
  bundle_ = SelfUpdate::bundlePath();
  if (!rel_.valid()) return fail(tr("The release has no installable download."));
  if (!rel_.verifiable()) return fail(tr("The release publishes no checksum for its download, so it cannot be verified."));
  const QString reason = SelfUpdate::unsupportedReason(bundle_);
  if (!reason.isEmpty()) return fail(reason);

  dir_ = QDir::tempPath() + "/ocat-update-" + QString::number(QCoreApplication::applicationPid());
  QDir(dir_).removeRecursively();
  if (!QDir().mkpath(dir_)) return fail(tr("A temporary folder could not be created."), false);
  const qint64 need = rel_.size > 0 ? rel_.size * 3 : 300LL * 1024 * 1024;  // dmg + mounted copy + staged copy
  if (QStorageInfo(dir_).bytesAvailable() < need || QStorageInfo(bundle_).bytesAvailable() < need)
    return fail(tr("There is not enough free disk space."), false);

  dmg_ = dir_ + "/" + rel_.name;
  file_.setFileName(dmg_);
  if (!file_.open(QIODevice::WriteOnly)) return fail(tr("The download file could not be created."), false);

  QNetworkRequest req{QUrl(rel_.url)};
  req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
  req.setRawHeader("User-Agent", QByteArray("OCAuxiliaryTools/") + CurVersion.toUtf8());
  reply_ = nam_.get(req);

  progress_ = new QProgressDialog(tr("Downloading %1 ...").arg(rel_.name), tr("Cancel"), 0,
                                  rel_.size > 0 ? int(qMin<qint64>(rel_.size / 1024, INT_MAX)) : 0, parent_);
  progress_->setWindowTitle(tr("Update to %1").arg(rel_.version));
  progress_->setWindowModality(Qt::WindowModal);
  progress_->setMinimumDuration(0);
  // By default the dialog hides itself at 100%: the verification that follows would run with no dialog and no
  // modality, and a second click on Download could start a second updater in the same temp folder.
  progress_->setAutoReset(false);
  progress_->setAutoClose(false);
  connect(progress_, &QProgressDialog::canceled, this, &SelfUpdater::cancel);

  connect(reply_, &QNetworkReply::readyRead, this, &SelfUpdater::onReadyRead);
  connect(reply_, &QNetworkReply::downloadProgress, this, &SelfUpdater::onProgress);
  connect(reply_, &QNetworkReply::finished, this, &SelfUpdater::onDownloaded);
  stall_.start(60000);
}

void SelfUpdater::onReadyRead() {
  if (writeFailed_) return;
  const QByteArray data = reply_->readAll();
  if (file_.write(data) != data.size()) {
    writeFailed_ = true;
    reply_->abort();
  }
}

void SelfUpdater::onProgress(qint64 got, qint64 total) {
  stall_.start(60000);  // data is still arriving
  if (!progress_) return;
  if (total > 0) {
    progress_->setMaximum(int(qMin<qint64>(total / 1024, INT_MAX)));
    progress_->setValue(int(qMin<qint64>(got / 1024, INT_MAX)));
  }
}

void SelfUpdater::cancel() {
  canceled_ = true;
  if (reply_) reply_->abort();
}

void SelfUpdater::onStalled() {
  if (reply_ && reply_->isRunning()) {
    stageError_ = tr("The download stalled.");
    reply_->abort();
  }
}

void SelfUpdater::onDownloaded() {
  stall_.stop();
  if (reply_->isOpen() && !writeFailed_) file_.write(reply_->readAll());
  file_.close();
  const QNetworkReply::NetworkError error = reply_->error();
  const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QString errorText = reply_->errorString();
  reply_->deleteLater();
  reply_ = nullptr;

  if (canceled_ && stageError_.isEmpty()) {  // the user cancelled: no message
    if (progress_) progress_->deleteLater();
    progress_ = nullptr;
    QDir(dir_).removeRecursively();
    deleteLater();
    return;
  }
  if (!stageError_.isEmpty()) return fail(stageError_);
  if (writeFailed_) return fail(tr("The download could not be written to disk."), false);
  if (error != QNetworkReply::NoError) return fail(errorText);
  if (status != 200) return fail(tr("The server answered with HTTP status %1.").arg(status));

  // Verify + stage off the GUI thread (mounting and signature checks take a few seconds).
  progress_->setLabelText(tr("Verifying the download ..."));
  progress_->setRange(0, 0);
  progress_->setCancelButton(nullptr);
  StageThread* t = new StageThread(rel_, dmg_, bundle_, QCoreApplication::applicationPid(), this);
  connect(t, &QThread::finished, this, [this, t]() {
    const bool ok = t->ok;
    stagedApp_ = t->app;
    stageError_ = t->err;
    t->deleteLater();
    if (ok)
      staged();
    else
      fail(stageError_);
  });
  t->start();
}

void SelfUpdater::staged() {
  if (progress_) {
    progress_->close();
    progress_->deleteLater();
    progress_ = nullptr;
  }
  QMessageBox box(QMessageBox::Question, tr("Update"),
                  tr("Version %1 has been downloaded and verified.").arg(rel_.version) + "\n\n" +
                      tr("Restart now to install it?"),
                  QMessageBox::NoButton, parent_);
  QPushButton* restart = box.addButton(tr("Restart now"), QMessageBox::AcceptRole);
  box.addButton(tr("Later"), QMessageBox::RejectRole);
  box.exec();
  if (box.clickedButton() != restart) {
    SelfUpdate::discard(stagedApp_);
    QDir(dir_).removeRecursively();
    deleteLater();
    return;
  }
  finish();
}

void SelfUpdater::finish() {
  // close() asks the main window to close (it prompts for unsaved changes) and says whether it did.
  QWidget* mainWin = parent_ ? parent_->window() : nullptr;
  if (mainWin && !mainWin->close()) {
    SelfUpdate::discard(stagedApp_);
    QDir(dir_).removeRecursively();
    QMessageBox::information(parent_, tr("Update"), tr("The update was not installed because the app was not closed."));
    deleteLater();
    return;
  }
  QString err;
  if (!SelfUpdate::launchSwap(bundle_, stagedApp_, QCoreApplication::applicationPid(), &err)) {
    SelfUpdate::discard(stagedApp_);
    QMessageBox::warning(nullptr, tr("Update"), err);
  }
  QDir(dir_).removeRecursively();
  QCoreApplication::quit();
}
