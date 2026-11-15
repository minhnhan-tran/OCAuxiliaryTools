#include "dlgMountESP.h"

#include "Method.h"
#include "mainwindow.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QUrl>

#include "ui_dlgMountESP.h"

extern MainWindow* mw_one;
extern Method* mymethod;
extern QString strIniFile;

dlgMountESP::dlgMountESP(QWidget* parent)
    : QDialog(parent), ui(new Ui::dlgMountESP) {
  ui->setupUi(this);
  ui->btnMountOpenConfig->setDefault(true);
}

dlgMountESP::~dlgMountESP() { delete ui; }

void dlgMountESP::on_btnMount_clicked() { mountESP(false); }

void dlgMountESP::on_btnMountOpenConfig_clicked() { mountESP(true); }

// "/Volumes/<name>/EFI/" for the selected list row, or empty when nothing is selected.
// The list item is "<disk> | <volume name> | ...".
static QString efiDirOf(const QListWidget* list) {
  if (!list->currentItem()) return QString();
  const QStringList parts = list->currentItem()->text().trimmed().simplified().split("|");
  if (parts.count() < 2) return QString();
  const QString name = parts.at(1).toLatin1().replace("?", "").trimmed();
  return name.isEmpty() ? QString() : "/Volumes/" + name + "/EFI/";
}

// Where `diskutil` says the partition is mounted, or empty when it is not mounted.
// Volume names are not enough: two ESPs both called "EFI" mount as /Volumes/EFI and /Volumes/EFI 1.
static QString mountPointOf(const QString& disk) {
  QProcess p;
  p.start("diskutil", {"info", disk});
  if (!p.waitForStarted(2000) || !p.waitForFinished(8000)) return QString();
  const QString out = QString::fromUtf8(p.readAllStandardOutput());
  for (const QString& line : out.split('\n'))
    if (line.trimmed().startsWith("Mount Point:")) return line.section(':', 1).trimmed();
  return QString();
}

static QString diskOfRow(const QListWidgetItem* it) {
  return it ? it->text().simplified().section('|', 0, 0).trimmed() : QString();
}

static QIcon dotIcon(const QColor& c) {
  QPixmap pm(24, 24);  // drawn at 2x so it stays sharp on Retina
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(c);
  p.drawEllipse(QRect(4, 4, 16, 16));
  p.end();
  pm.setDevicePixelRatio(2);
  return QIcon(pm);
}

void dlgMountESP::refreshStates() {
#ifdef Q_OS_MAC
  static const QIcon on = dotIcon(QColor("#34c759")), off = dotIcon(QColor("#8e8e93"));
  // Halfway between text and background: readable on light and dark themes alike (the style's
  // own "disabled" grey is only ~1.9:1 on white).
  const QPalette pal = ui->listWidget->palette();
  const QColor fg = pal.color(QPalette::Text), bg = pal.color(QPalette::Base);
  const QColor dim((fg.red() + bg.red()) / 2, (fg.green() + bg.green()) / 2, (fg.blue() + bg.blue()) / 2);
  for (int i = 0; i < ui->listWidget->count(); i++) {
    QListWidgetItem* it = ui->listWidget->item(i);
    const QString mp = mountPointOf(diskOfRow(it));
    it->setData(Qt::UserRole, mp);
    QFont f = it->font();
    f.setBold(!mp.isEmpty());
    it->setFont(f);
    it->setIcon(mp.isEmpty() ? off : on);
    it->setForeground(mp.isEmpty() ? QBrush(dim) : QBrush());  // default brush = normal text colour
    it->setToolTip(mp.isEmpty() ? tr("Not mounted") : tr("Mounted at %1").arg(mp));
  }
#endif
  updateButtons();
}

// "Open EFI folder" only makes sense for a mounted partition (nothing to open otherwise).
void dlgMountESP::updateButtons() {
  const QListWidgetItem* it = ui->listWidget->currentItem();
#ifdef Q_OS_MAC
  const bool mounted = it && !it->data(Qt::UserRole).toString().isEmpty();
  ui->btnOpenEfiFolder->setEnabled(mounted);
  ui->btnOpenEfiFolder->setToolTip(mounted ? tr("Open the EFI folder of the selected partition")
                                           : tr("The selected partition is not mounted. Use Mount first."));
#else
  ui->btnOpenEfiFolder->setEnabled(it != nullptr);
#endif
}

void dlgMountESP::on_listWidget_currentRowChanged(int) { updateButtons(); }

void dlgMountESP::on_btnOpenEfiFolder_clicked() {
  if (!ui->listWidget->currentItem()) return;
  const QString disk = diskOfRow(ui->listWidget->currentItem());
  QString dirpath;
  const QString mp = mountPointOf(disk);
  if (!mp.isEmpty()) dirpath = QFileInfo(mp + "/EFI").isDir() ? mp + "/EFI/" : mp + "/";
  else dirpath = efiDirOf(ui->listWidget);  // no diskutil (not macOS): fall back to the volume name
  if (dirpath.isEmpty() || !QFileInfo(dirpath).isDir()) {
    QMessageBox::information(this, windowTitle(), tr("%1 is not mounted. Mount the partition first (Mount).").arg(disk));
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(dirpath));
}

void dlgMountESP::mountESP(bool openConfig) {
  if (ui->listWidget->count() == 0) return;

  QSettings Reg(strIniFile, QSettings::IniFormat);
  Reg.setValue("mesp", ui->listWidget->currentRow());

  QString str = ui->listWidget->currentItem()->text().trimmed();
  QStringList strList = str.simplified().split("|");
  QString strDisk;
  if (strList.count() > 0) strDisk = strList.at(0);
  strDisk = strDisk.trimmed();
  mymethod->mount_esp_mac(strDisk);

  QString str0 = strList.at(1);
  QString str1 = str0.toLatin1();
  QString str2 = str1.replace("?", "");
  QString dirpath = "/Volumes/" + str2.trimmed() + "/EFI/";
  qDebug() << dirpath;
  QString dir = "file:" + dirpath;

  if (openConfig) {
    QString strConfig = dirpath + "OC/config.plist";
    QFileInfo fi(strConfig.toLower());
    if (fi.exists()) mw_one->openFile(strConfig);
  } else if (QDesktopServices::openUrl(QUrl(dir, QUrl::TolerantMode))) {
  }

  close();
  delete this;
}
