#include "dlgusbmap.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QProcess>
#include <QUrl>
#include <QVBoxLayout>

using namespace UsbMap;

namespace {
enum Col { ColOn, ColCtrl, ColPort, ColType, ColConn, ColNick, ColDev, ColCount };
const int RoleCtrl = Qt::UserRole;
const int RoleKey = Qt::UserRole + 1;

bool onMac() {
#ifdef Q_OS_MAC
  return true;
#else
  return false;
#endif
}
}  // namespace

QString dlgUsbMap::stateDir() { return QDir::homePath() + "/.ocat/USBMap"; }

dlgUsbMap::dlgUsbMap(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("USB Mapping"));
  resize(980, 620);

  hint = new QLabel(tr(
      "1. Click <b>Discover ports</b>. 2. Plug a USB 2 device, then a USB 3 device, into every "
      "physical port, clicking Discover after each one: ports that have a device are remembered. "
      "3. Tick the ports you want to keep (macOS allows at most 15 per controller), choose the "
      "connector type and a nickname, then <b>Build USBMap.kext</b>. "
      "Based on <a href=\"https://github.com/corpnewt/USBMap\">corpnewt/USBMap</a> (MIT)."));
  hint->setWordWrap(true);
  hint->setOpenExternalLinks(true);

  btnDiscover = new QPushButton(tr("Discover ports"));
  btnDump = new QPushButton(tr("Load ioreg dump..."));
  btnDump->setToolTip(tr("Use a saved `ioreg -l -p IOService -w0` text file instead of this Mac."));
  QPushButton* btnDev = new QPushButton(tr("Tick ports with devices"));
  QPushButton* btnAll = new QPushButton(tr("Tick all"));
  QPushButton* btnNone = new QPushButton(tr("Untick all"));
  QPushButton* btnReset = new QPushButton(tr("Reset"));
  btnReset->setToolTip(tr("Forget the saved map and start again."));
  btnDiscover->setEnabled(onMac());
  if (!onMac()) btnDiscover->setToolTip(tr("Discovery needs macOS (ioreg). Use \"Load ioreg dump\"."));

  QHBoxLayout* top = new QHBoxLayout;
  top->addWidget(btnDiscover);
  top->addWidget(btnDump);
  top->addSpacing(16);
  top->addWidget(btnDev);
  top->addWidget(btnAll);
  top->addWidget(btnNone);
  top->addStretch();
  top->addWidget(btnReset);

  table = new QTableWidget(0, ColCount, this);
  table->setHorizontalHeaderLabels(QStringList() << tr("On") << tr("Controller") << tr("Port")
                                                 << tr("Class") << tr("Connector type")
                                                 << tr("Nickname") << tr("Devices seen"));
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  table->horizontalHeader()->setStretchLastSection(true);
  table->setColumnWidth(ColOn, 40);
  table->setColumnWidth(ColCtrl, 140);
  table->setColumnWidth(ColPort, 90);
  table->setColumnWidth(ColType, 150);
  table->setColumnWidth(ColConn, 170);
  table->setColumnWidth(ColNick, 200);
  table->verticalHeader()->setVisible(false);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);

  status = new QLabel;
  status->setWordWrap(true);
  chkLegacy = new QCheckBox(tr("macOS 10.11 - 10.14 (legacy AppleUSBMergeNub)"));
  btnBuild = new QPushButton(tr("Build USBMap.kext..."));
  QPushButton* btnClose = new QPushButton(tr("Close"));
  QHBoxLayout* bottom = new QHBoxLayout;
  bottom->addWidget(chkLegacy);
  bottom->addStretch();
  bottom->addWidget(btnBuild);
  bottom->addWidget(btnClose);

  QVBoxLayout* lay = new QVBoxLayout(this);
  lay->addWidget(hint);
  lay->addLayout(top);
  lay->addWidget(table, 1);
  lay->addWidget(status);
  lay->addLayout(bottom);

  connect(btnDiscover, &QPushButton::clicked, this, &dlgUsbMap::discoverFromIoreg);
  connect(btnDump, &QPushButton::clicked, this, &dlgUsbMap::loadDump);
  connect(btnDev, &QPushButton::clicked, this, &dlgUsbMap::enableWithDevices);
  connect(btnAll, &QPushButton::clicked, this, [this] { setAll(1); });
  connect(btnNone, &QPushButton::clicked, this, [this] { setAll(0); });
  connect(btnReset, &QPushButton::clicked, this, &dlgUsbMap::resetMap);
  connect(btnBuild, &QPushButton::clicked, this, &dlgUsbMap::buildKext);
  connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
  connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
    if (filling) return;
    Port* p = portAt(it->row());
    if (!p) return;
    if (it->column() == ColOn) p->enabled = it->checkState() == Qt::Checked ? 1 : 0;
    else if (it->column() == ColNick) p->comment = it->text().trimmed();
    updateStatus();
  });

  loadState();
  fillTable();
  updateStatus();
}

dlgUsbMap::~dlgUsbMap() { saveState(); }

Port* dlgUsbMap::portAt(int row) {
  QTableWidgetItem* it = table->item(row, ColCtrl);
  if (!it) return nullptr;
  const int c = it->data(RoleCtrl).toInt();
  const QString key = it->data(RoleKey).toString();
  if (c < 0 || c >= list.size() || !list[c].ports.contains(key)) return nullptr;
  return &list[c].ports[key];
}

void dlgUsbMap::saveState() const {
  if (list.isEmpty()) return;
  QDir().mkpath(stateDir());
  QFile f(stateDir() + "/map.json");
  if (f.open(QIODevice::WriteOnly)) f.write(toJson(list, smbios));
}

void dlgUsbMap::loadState() {
  QFile f(stateDir() + "/map.json");
  if (f.open(QIODevice::ReadOnly)) fromJson(f.readAll(), &list, &smbios);
}

void dlgUsbMap::fillTable() {
  filling = true;
  table->setRowCount(0);
  const QList<QPair<int, QString> > types = connectorTypes();
  for (int c = 0; c < list.size(); ++c) {
    for (QMap<QString, Port>::const_iterator it = list[c].ports.begin(); it != list[c].ports.end(); ++it) {
      const Port& p = it.value();
      const int row = table->rowCount();
      table->insertRow(row);
      QTableWidgetItem* on = new QTableWidgetItem;
      on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      on->setCheckState(p.enabled == 1 ? Qt::Checked : Qt::Unchecked);
      table->setItem(row, ColOn, on);
      QTableWidgetItem* ctrl = new QTableWidgetItem(list[c].key);
      ctrl->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      ctrl->setData(RoleCtrl, c);
      ctrl->setData(RoleKey, p.key);
      table->setItem(row, ColCtrl, ctrl);
      const QString pn = QString("%1 (%2)").arg(p.name, p.key.left(2).toUpper());
      const QStringList ro = QStringList() << pn << p.type;
      for (int k = 0; k < 2; ++k) {
        QTableWidgetItem* t = new QTableWidgetItem(ro[k]);
        t->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table->setItem(row, ColPort + k, t);
      }
      QComboBox* cb = new QComboBox;
      cb->addItem(tr("Auto (as detected: %1)").arg(p.connector < 0 ? tr("none") : QString::number(p.connector)), -1);
      for (int k = 0; k < types.size(); ++k)
        cb->addItem(QString("%1 - %2").arg(types[k].first).arg(types[k].second), types[k].first);
      const int at = cb->findData(p.typeOverride);
      cb->setCurrentIndex(at < 0 ? 0 : at);
      connect(cb, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, cb, row](int) {
        if (filling) return;
        if (Port* pp = portAt(row)) pp->typeOverride = cb->currentData().toInt();
      });
      table->setCellWidget(row, ColConn, cb);
      table->setItem(row, ColNick, new QTableWidgetItem(p.comment));
      QTableWidgetItem* dev = new QTableWidgetItem(p.items.join(", "));
      dev->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      table->setItem(row, ColDev, dev);
    }
  }
  filling = false;
}

void dlgUsbMap::updateStatus() {
  int ports = 0, on = 0, withDev = 0;
  QStringList over;
  for (int c = 0; c < list.size(); ++c) {
    ports += list[c].ports.size();
    const int n = enabledCount(list[c]);
    on += n;
    if (n > kPortLimit) over << QString("%1 (%2)").arg(list[c].key).arg(n);
    for (QMap<QString, Port>::const_iterator it = list[c].ports.begin(); it != list[c].ports.end(); ++it)
      if (!it.value().items.isEmpty()) ++withDev;
  }
  QString s = list.isEmpty()
                  ? tr("Nothing discovered yet.")
                  : tr("%1: %2 controller(s), %3 port(s), %4 ticked, %5 with a device seen.")
                        .arg(smbios.isEmpty() ? tr("Unknown SMBIOS") : smbios)
                        .arg(list.size()).arg(ports).arg(on).arg(withDev);
  if (!over.isEmpty())
    s += "  <span style=\"color:#c62828\"><b>" +
         tr("More than %1 ports ticked on: %2. macOS ignores the extra ports.").arg(kPortLimit).arg(over.join(", ")) +
         "</span>";
  status->setText(s);
  btnBuild->setEnabled(on > 0);
}

void dlgUsbMap::applyDiscovery(const QString& text) {
  ControllerList fresh;
  QString sm, err;
  if (!discover(sanitizeIoreg(text), &fresh, &sm, &err)) {
    QMessageBox::warning(this, tr("USB Mapping"), err);
    return;
  }
  merge(&list, fresh);
  if (!sm.isEmpty()) smbios = sm;
  fillTable();
  updateStatus();
  saveState();
}

void dlgUsbMap::discoverFromIoreg() {
  QProcess p;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  p.start("/usr/sbin/ioreg", QStringList() << "-l" << "-p" << "IOService" << "-w0");
  const bool ok = p.waitForFinished(120000) && p.exitCode() == 0;
  QApplication::restoreOverrideCursor();
  if (!ok) {
    QMessageBox::warning(this, tr("USB Mapping"), tr("ioreg could not be run or took too long."));
    return;
  }
  applyDiscovery(QString::fromUtf8(p.readAllStandardOutput()));
}

void dlgUsbMap::loadDump() {
  const QString f = QFileDialog::getOpenFileName(this, tr("ioreg dump"), QDir::homePath(), tr("Text (*.txt *.log);;All files (*)"));
  if (f.isEmpty()) return;
  QFile file(f);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("USB Mapping"), tr("Cannot read %1").arg(f));
    return;
  }
  applyDiscovery(QString::fromUtf8(file.readAll()));
}

void dlgUsbMap::setAll(int state) {
  for (int c = 0; c < list.size(); ++c)
    for (QMap<QString, Port>::iterator it = list[c].ports.begin(); it != list[c].ports.end(); ++it)
      it.value().enabled = state;
  fillTable();
  updateStatus();
}

void dlgUsbMap::enableWithDevices() {
  for (int c = 0; c < list.size(); ++c)
    for (QMap<QString, Port>::iterator it = list[c].ports.begin(); it != list[c].ports.end(); ++it)
      it.value().enabled = it.value().items.isEmpty() ? 0 : 1;
  fillTable();
  updateStatus();
}

void dlgUsbMap::resetMap() {
  if (QMessageBox::question(this, tr("USB Mapping"), tr("Forget the saved map, nicknames and connector types?")) != QMessageBox::Yes) return;
  list.clear();
  smbios.clear();
  QFile::remove(stateDir() + "/map.json");
  fillTable();
  updateStatus();
}

void dlgUsbMap::buildKext() {
  for (int c = 0; c < list.size(); ++c) {
    if (enabledCount(list[c]) > kPortLimit &&
        QMessageBox::question(this, tr("USB Mapping"),
                              tr("%1 has more than %2 ports ticked; macOS ignores the extra ports. Build anyway?")
                                  .arg(list[c].key).arg(kPortLimit)) != QMessageBox::Yes)
      return;
  }
  QDir().mkpath(stateDir() + "/Results");
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Where to save the kext"), stateDir() + "/Results");
  if (dir.isEmpty()) return;
  QString err;
  const QString kext = writeKext(dir, list, smbios, chkLegacy->isChecked(), &err);
  if (kext.isEmpty()) {
    QMessageBox::warning(this, tr("USB Mapping"), err);
    return;
  }
  saveState();
  QMessageBox box(QMessageBox::Information, tr("USB Mapping"),
                  tr("Saved %1\n\nNext: copy it to EFI/OC/Kexts, add it in config.plist > Kernel > Add, "
                     "remove USBInjectAll if you have it, then turn Kernel > Quirks > XhciPortLimit off "
                     "and reboot.").arg(kext), QMessageBox::Ok, this);
  QPushButton* show = box.addButton(tr("Show in folder"), QMessageBox::ActionRole);
  box.exec();
  if (box.clickedButton() == show) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
