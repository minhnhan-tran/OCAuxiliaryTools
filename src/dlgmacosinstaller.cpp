#include "dlgmacosinstaller.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProcess>
#include <QStorageInfo>
#include <QDir>
#include <QVBoxLayout>

#include "postcheck/postcheck.h"

using namespace MacInstaller;

static QString gb(qint64 bytes) { return QString::number(bytes / 1e9, 'f', bytes >= 100e9 ? 0 : 1) + " GB"; }

dlgMacOSInstaller::dlgMacOSInstaller(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Create macOS Installation"));
  resize(760, 640);
  QVBoxLayout* top = new QVBoxLayout(this);

#ifndef Q_OS_MAC
  top->addWidget(new QLabel(tr("Creating a macOS installer needs Apple's createinstallmedia, which only runs on a Mac.")));
  QPushButton* close = new QPushButton(tr("Close"));
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  top->addWidget(close);
  cboInstaller = cboApple = cboDisk = nullptr;
  return;
#else
  QLabel* intro = new QLabel(tr("Make a bootable macOS installer USB drive with Apple's createinstallmedia, the same method "
                                "OpenCore Legacy Patcher uses. Nothing runs from here: the command is opened in Terminal and "
                                "starts only after you confirm, and you type your password there."));
  intro->setWordWrap(true);
  top->addWidget(intro);

  QGroupBox* g1 = new QGroupBox(tr("1. macOS installer"));
  QGridLayout* l1 = new QGridLayout(g1);
  cboInstaller = new QComboBox;
  QPushButton* bRefresh = new QPushButton(tr("Refresh"));
  QPushButton* bBrowse = new QPushButton(tr("Browse..."));
  l1->addWidget(new QLabel(tr("Installed:")), 0, 0);
  l1->addWidget(cboInstaller, 0, 1);
  l1->addWidget(bBrowse, 0, 2);
  l1->addWidget(bRefresh, 0, 3);
  lblInstaller = new QLabel;
  lblInstaller->setWordWrap(true);
  l1->addWidget(lblInstaller, 1, 0, 1, 4);
  cboApple = new QComboBox;
  btnLoad = new QPushButton(tr("Load list"));
  l1->addWidget(new QLabel(tr("From Apple:")), 2, 0);
  l1->addWidget(cboApple, 2, 1);
  l1->addWidget(btnLoad, 2, 2, 1, 2);
  chkBeta = new QCheckBox(tr("Show beta / seed builds"));
  l1->addWidget(chkBeta, 3, 1, 1, 3);
  editFolder = new QLineEdit(QDir::homePath() + "/Downloads");
  QPushButton* bFolder = new QPushButton(tr("Browse..."));
  l1->addWidget(new QLabel(tr("Save to:")), 4, 0);
  l1->addWidget(editFolder, 4, 1);
  l1->addWidget(bFolder, 4, 2, 1, 2);
  btnDownload = new QPushButton(tr("Download"));
  btnCancel = new QPushButton(tr("Cancel"));
  btnInstallPkg = new QPushButton(tr("Install to /Applications..."));
  btnDownload->setEnabled(false);
  btnCancel->setEnabled(false);
  btnInstallPkg->setEnabled(false);
  QHBoxLayout* dl = new QHBoxLayout;
  dl->addWidget(btnDownload);
  dl->addWidget(btnCancel);
  dl->addWidget(btnInstallPkg);
  dl->addStretch(1);
  l1->addLayout(dl, 5, 0, 1, 4);
  bar = new QProgressBar;
  bar->setRange(0, 1000);
  bar->setTextVisible(true);
  bar->setVisible(false);
  l1->addWidget(bar, 6, 0, 1, 4);
  lblApple = new QLabel(tr("Press \"Load list\" to read Apple's installer catalog."));
  lblApple->setWordWrap(true);
  l1->addWidget(lblApple, 7, 0, 1, 4);
  l1->setColumnStretch(1, 1);
  top->addWidget(g1);

  QGroupBox* g2 = new QGroupBox(tr("2. USB drive (it will be ERASED)"));
  QGridLayout* l2 = new QGridLayout(g2);
  cboDisk = new QComboBox;
  QPushButton* bDisks = new QPushButton(tr("Refresh"));
  l2->addWidget(cboDisk, 0, 0);
  l2->addWidget(bDisks, 0, 1);
  lblDisk = new QLabel;
  lblDisk->setWordWrap(true);
  l2->addWidget(lblDisk, 1, 0, 1, 2);
  l2->setColumnStretch(0, 1);
  top->addWidget(g2);

  QGroupBox* g3 = new QGroupBox(tr("3. OpenCore EFI (optional)"));
  QGridLayout* l3 = new QGridLayout(g3);
  chkEfi = new QCheckBox(tr("Copy an EFI folder onto the USB so it boots with OpenCore"));
  editEfi = new QLineEdit;
  editEfi->setPlaceholderText(tr("Path of the EFI folder (contains BOOT and OC)"));
  QPushButton* bEfi = new QPushButton(tr("Browse..."));
  l3->addWidget(chkEfi, 0, 0, 1, 2);
  l3->addWidget(editEfi, 1, 0);
  l3->addWidget(bEfi, 1, 1);
  lblEfi = new QLabel;
  lblEfi->setWordWrap(true);
  l3->addWidget(lblEfi, 2, 0, 1, 2);
  l3->setColumnStretch(0, 1);
  top->addWidget(g3);

  top->addWidget(new QLabel(tr("Command (review it; it is run in Terminal):")));
  preview = new QPlainTextEdit;
  preview->setReadOnly(true);
  preview->setMaximumHeight(110);
  top->addWidget(preview);

  QHBoxLayout* bl = new QHBoxLayout;
  btnCopy = new QPushButton(tr("Copy command"));
  btnCreate = new QPushButton(tr("Create installer in Terminal..."));
  QPushButton* bClose = new QPushButton(tr("Close"));
  bl->addStretch(1);
  bl->addWidget(btnCopy);
  bl->addWidget(btnCreate);
  bl->addWidget(bClose);
  top->addLayout(bl);

  connect(bRefresh, &QPushButton::clicked, this, &dlgMacOSInstaller::refreshInstallers);
  connect(bBrowse, &QPushButton::clicked, this, &dlgMacOSInstaller::browseInstaller);
  connect(bDisks, &QPushButton::clicked, this, &dlgMacOSInstaller::refreshDisks);
  connect(bEfi, &QPushButton::clicked, this, &dlgMacOSInstaller::browseEfi);
  connect(btnDownload, &QPushButton::clicked, this, &dlgMacOSInstaller::downloadSelected);
  connect(btnCancel, &QPushButton::clicked, this, &dlgMacOSInstaller::cancelDownload);
  connect(btnInstallPkg, &QPushButton::clicked, this, &dlgMacOSInstaller::installDownloaded);
  connect(btnLoad, &QPushButton::clicked, this, &dlgMacOSInstaller::fetchAppleList);
  connect(bFolder, &QPushButton::clicked, this, &dlgMacOSInstaller::browseFolder);
  connect(chkBeta, &QCheckBox::toggled, this, [this] { updateDownloadButtons(); });
  connect(editFolder, &QLineEdit::textChanged, this, [this] { updateDownloadButtons(); });
  connect(btnCreate, &QPushButton::clicked, this, &dlgMacOSInstaller::createUsb);
  connect(bClose, &QPushButton::clicked, this, &QDialog::accept);
  connect(btnCopy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(command); });
  connect(cboInstaller, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateCommand(); });
  connect(cboDisk, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateCommand(); });
  connect(cboApple, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateDownloadButtons(); });
  connect(chkEfi, &QCheckBox::toggled, this, [this] { updateCommand(); });
  connect(editEfi, &QLineEdit::textChanged, this, [this] { updateCommand(); });

  refreshInstallers();
  refreshDisks();
  fetchAppleList();
  updateCommand();
  updateDownloadButtons();
#endif
}

#ifdef Q_OS_MAC
QString dlgMacOSInstaller::currentInstaller() const { return cboInstaller->currentData().toString(); }

bool dlgMacOSInstaller::currentDisk(Disk* d) const {
  const int i = cboDisk->currentIndex();
  if (i < 0) return false;
  d->id = cboDisk->itemData(i).toString();
  d->size = cboDisk->itemData(i, Qt::UserRole + 1).toLongLong();
  d->name = cboDisk->itemData(i, Qt::UserRole + 2).toString();
  d->bus = cboDisk->itemData(i, Qt::UserRole + 3).toString();
  return validDiskId(d->id);
}

void dlgMacOSInstaller::refreshInstallers() {
  const QString keep = currentInstaller();
  cboInstaller->blockSignals(true);
  cboInstaller->clear();
  QStringList seen;
  for (const Installer& in : findInstallers()) {
    cboInstaller->addItem(in.name + (in.version.isEmpty() ? "" : "  (" + in.version + ")"), in.path);
    seen << in.path;
  }
  if (!extraInstaller.isEmpty() && !seen.contains(extraInstaller))
    cboInstaller->addItem(QFileInfo(extraInstaller).completeBaseName(), extraInstaller);
  int idx = cboInstaller->findData(keep);
  cboInstaller->setCurrentIndex(idx >= 0 ? idx : (cboInstaller->count() ? 0 : -1));
  cboInstaller->blockSignals(false);
  lblInstaller->setText(cboInstaller->count()
      ? QString()
      : tr("No \"Install macOS ...app\" found in /Applications. Download one from Apple below, or Browse to one."));
  updateCommand();
}

void dlgMacOSInstaller::browseInstaller() {
  // An .app bundle is a folder, so the directory chooser is the right dialog.
  QString app = QFileDialog::getExistingDirectory(this, tr("Choose an \"Install macOS\" app"), "/Applications");
  if (app.isEmpty()) return;
  while (app.endsWith('/')) app.chop(1);
  if (!QFileInfo(app + "/Contents/Resources/createinstallmedia").isExecutable()) {
    QMessageBox::warning(this, windowTitle(), tr("That is not a macOS installer: Contents/Resources/createinstallmedia is missing."));
    return;
  }
  extraInstaller = app;
  refreshInstallers();
  const int i = cboInstaller->findData(app);
  if (i >= 0) cboInstaller->setCurrentIndex(i);
}

void dlgMacOSInstaller::refreshDisks() {
  cboDisk->blockSignals(true);
  cboDisk->clear();
  QStringList skipped;
  QString err;
  const QList<Disk> disks = listExternalDisks(&skipped, &err);
  for (const Disk& d : disks) {
    cboDisk->addItem(QString("%1  -  %2  %3 (%4)").arg(d.id, d.name.isEmpty() ? tr("Untitled") : d.name, gb(d.size), d.bus), d.id);
    const int i = cboDisk->count() - 1;
    cboDisk->setItemData(i, d.size, Qt::UserRole + 1);
    cboDisk->setItemData(i, d.name, Qt::UserRole + 2);
    cboDisk->setItemData(i, d.bus, Qt::UserRole + 3);
  }
  cboDisk->setCurrentIndex(disks.isEmpty() ? -1 : 0);
  cboDisk->blockSignals(false);
  QString msg;
  if (!err.isEmpty()) msg = tr("Could not list disks: %1").arg(err);
  else if (disks.isEmpty()) msg = tr("No external USB drive of 16 GB or more found. Plug one in and press Refresh.");
  else msg = tr("Only external drives are listed; the disk macOS is running from is never offered. Everything on the chosen drive is erased.");
  if (!skipped.isEmpty()) msg += "\n" + tr("Not offered: %1").arg(skipped.join("; "));
  lblDisk->setText(msg);
  updateCommand();
}

void dlgMacOSInstaller::fetchAppleList() {
  if (loader) return;
  btnLoad->setEnabled(false);
  lblApple->setText(tr("Reading Apple's installer catalog..."));
  loader = new MacInstaller::CatalogLoader(&nam, this);
  connect(loader, &CatalogLoader::finished, this, [this](const QList<Product>& list, const QString& error) {
    loader->deleteLater();
    loader = nullptr;
    btnLoad->setEnabled(true);
    appleList = list;
    if (!error.isEmpty()) lblApple->setText(error);
    else lblApple->setText(tr("%1 installers from Apple. The file is a signed InstallAssistant.pkg; it is checked, then installed into /Applications.").arg(list.size()));
    updateDownloadButtons();
  });
  loader->start();
}

QString dlgMacOSInstaller::downloadPath(const Product& p) const {
  const QString name = pkgFileName(p);
  return name.isEmpty() ? QString() : QDir(editFolder->text().trimmed()).filePath(name);
}

void dlgMacOSInstaller::updateDownloadButtons() {
  const QString keep = cboApple->currentData().toString();
  cboApple->blockSignals(true);
  cboApple->clear();
  shown.clear();
  for (const Product& p : appleList) {
    if (!chkBeta->isChecked() && isBetaBuild(p.build)) continue;
    shown << p;
    cboApple->addItem(QString("%1 %2  (%3, %4)").arg(p.title, p.version, p.build, humanSize(p.size)), p.id);
  }
  int idx = cboApple->findData(keep);
  cboApple->setCurrentIndex(idx >= 0 ? idx : (cboApple->count() ? 0 : -1));
  cboApple->blockSignals(false);
  const bool busy = downloader && downloader->running();
  const int i = cboApple->currentIndex();
  const bool ok = !busy && i >= 0 && i < shown.size() && !downloadPath(shown[i]).isEmpty() && QFileInfo(editFolder->text().trimmed()).isDir();
  btnDownload->setEnabled(ok);
  btnCancel->setEnabled(busy);
  btnLoad->setEnabled(!loader && !busy);
  cboApple->setEnabled(!busy);
  btnInstallPkg->setEnabled(!busy && !downloadedPkg.isEmpty());
  if (ok && QFileInfo::exists(downloadPath(shown[i]) + ".part"))
    btnDownload->setText(tr("Resume download"));
  else
    btnDownload->setText(tr("Download"));
}

void dlgMacOSInstaller::browseFolder() {
  const QString d = QFileDialog::getExistingDirectory(this, tr("Save the installer to"), editFolder->text());
  if (!d.isEmpty()) editFolder->setText(d);
}

void dlgMacOSInstaller::downloadSelected() {
  const int i = cboApple->currentIndex();
  if (i < 0 || i >= shown.size() || (downloader && downloader->running())) return;
  const Product p = shown[i];
  const QString path = downloadPath(p);
  if (path.isEmpty()) return;
  downloadedPkg.clear();
  // The pkg is as big as the installer, and installing it needs about the same again.
  const QString dir = editFolder->text().trimmed();
  const qint64 have = QStorageInfo(dir).bytesAvailable();
  const qint64 partial = QFileInfo(path + ".part").exists() ? QFileInfo(path + ".part").size() : 0;
  if (!QFileInfo::exists(path) && have >= 0 && have < p.size - partial + 1000000000LL) {
    QMessageBox::warning(this, windowTitle(), tr("Not enough free space in %1: %2 free, %3 needed. Pick another folder.")
                                                  .arg(dir, humanSize(have), humanSize(p.size - partial)));
    return;
  }
  if (!downloader) {
    downloader = new MacInstaller::Downloader(&nam, this);
    connect(downloader, &Downloader::progress, this, [this](qint64 done, qint64 total) {
      bar->setVisible(true);
      bar->setValue(total > 0 ? int(done * 1000 / total) : 0);
      bar->setFormat(QString("%1 / %2").arg(humanSize(done), humanSize(total)));
    });
    connect(downloader, &Downloader::finished, this, &dlgMacOSInstaller::downloadFinished);
  }
  if (QFileInfo::exists(path)) {  // already downloaded: just verify it again
    downloadedPkg = path;
    downloadFinished(true, QString());
    return;
  }
  QString err;
  lblApple->setText(tr("Downloading %1 %2...").arg(p.title, p.version));
  // Set before start(): a complete leftover .part file finishes synchronously and is verified
  // from downloadFinished(), which reads downloadedPkg.
  downloadedPkg = path;
  if (!downloader->start(p.pkgUrl, path, p.size, &err)) {
    downloadedPkg.clear();
    lblApple->setText(err);
    updateDownloadButtons();
    return;
  }
  if (downloader->running()) updateDownloadButtons();
}

void dlgMacOSInstaller::cancelDownload() {
  if (downloader) downloader->cancel();
}

void dlgMacOSInstaller::downloadFinished(bool ok, const QString& error) {
  if (!ok) {
    downloadedPkg.clear();
    lblApple->setText(error);
    updateDownloadButtons();
    return;
  }
  // Apple signs the pkg; refuse anything else before it can reach `installer`.
  QProcess p;
  p.start("pkgutil", QStringList() << "--check-signature" << downloadedPkg);
  const bool done = p.waitForFinished(120000);
  const QString out = QString::fromUtf8(p.readAllStandardOutput()) + QString::fromUtf8(p.readAllStandardError());
  if (!done || p.exitCode() != 0 || !packageSigned(out)) {
    lblApple->setText(tr("The downloaded file is not a package signed by Apple, so it was NOT trusted: %1\nDelete it and download again.").arg(downloadedPkg));
    downloadedPkg.clear();
    updateDownloadButtons();
    return;
  }
  bar->setVisible(false);
  lblApple->setText(tr("Downloaded and verified (Apple signature): %1\nPress \"Install to /Applications...\" to unpack the installer app.").arg(downloadedPkg));
  updateDownloadButtons();
}

void dlgMacOSInstaller::installDownloaded() {
  const QString cmd = installPkgCommand(downloadedPkg);
  if (cmd.isEmpty() || !QFileInfo::exists(downloadedPkg)) return;
  QString err;
  if (!PostCheck::openInTerminal(cmd, &err)) {
    QMessageBox::warning(this, windowTitle(), tr("Could not open Terminal: %1").arg(err));
    return;
  }
  lblApple->setText(tr("Terminal is installing the package. Enter your password there; when it says \"The install was successful\", press Refresh next to \"Installed\"."));
}

void dlgMacOSInstaller::browseEfi() {
  const QString d = QFileDialog::getExistingDirectory(this, tr("Choose the EFI folder"), QDir::homePath());
  if (d.isEmpty()) return;
  editEfi->setText(d);
  chkEfi->setChecked(true);
}

void dlgMacOSInstaller::updateCommand() {
  command.clear();
  Disk d;
  const bool haveDisk = currentDisk(&d);
  const QString app = currentInstaller();
  QString efi;
  lblEfi->clear();
  bool efiOk = true;
  if (chkEfi->isChecked()) {
    const QString why = checkEfiFolder(editEfi->text().trimmed());
    if (!why.isEmpty()) { lblEfi->setText(why); efiOk = false; }
    else efi = editEfi->text().trimmed();
  }
  QString missing;
  if (app.isEmpty()) missing = tr("Choose a macOS installer.");
  else if (!haveDisk) missing = tr("Choose a USB drive.");
  else if (!efiOk) missing = tr("Fix the EFI folder or untick the option.");
  if (missing.isEmpty()) command = createCommand(app, d, efi);
  if (command.isEmpty() && missing.isEmpty()) missing = tr("The command could not be built for these choices.");
  preview->setPlainText(missing.isEmpty() ? command : missing);
  btnCreate->setEnabled(!command.isEmpty());
  btnCopy->setEnabled(!command.isEmpty());
}

void dlgMacOSInstaller::createUsb() {
  Disk d;
  if (command.isEmpty() || !currentDisk(&d)) return;
  // The list may be stale (drive swapped since): look at the disk again right before the erase.
  QStringList skipped;
  QString err;
  bool still = false;
  for (const Disk& x : listExternalDisks(&skipped, &err))
    if (x.id == d.id && x.size == d.size && x.name == d.name) still = true;
  if (!still) {
    QMessageBox::warning(this, windowTitle(), tr("%1 changed or is no longer an eligible external drive. The list has been refreshed.").arg(d.id));
    refreshDisks();
    return;
  }
  QMessageBox box(QMessageBox::Warning, tr("Erase this drive?"),
                  tr("<b>%1</b> (%2, %3) will be completely erased and turned into a macOS installer.<br><br>"
                     "Check that this is the right drive; the data on it cannot be recovered.")
                      .arg(d.id, d.name.isEmpty() ? tr("Untitled") : d.name.toHtmlEscaped(), gb(d.size)),
                  QMessageBox::NoButton, this);
  QPushButton* go = box.addButton(tr("Erase and create"), QMessageBox::DestructiveRole);
  box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(QMessageBox::Cancel);
  box.exec();
  if (box.clickedButton() != go) return;
  if (!PostCheck::openInTerminal(command, &err)) QMessageBox::warning(this, windowTitle(), tr("Could not open Terminal: %1").arg(err));
  else QMessageBox::information(this, windowTitle(), tr("Terminal is running the command. Enter your password there and wait for \"Install media now available\"."));
}
#endif
