#ifndef DLGUSBMAP_H
#define DLGUSBMAP_H

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

#include "usbmap/usbmap.h"

// Tools > USB Mapping: a native port of corpnewt/USBMap (MIT). Discover the USB
// ports macOS sees, plug devices in to find which ports are real, pick the
// connector type / nickname, then build USBMap.kext.
class dlgUsbMap : public QDialog {
  Q_OBJECT
 public:
  explicit dlgUsbMap(QWidget* parent = nullptr);
  ~dlgUsbMap() override;

 private:
  void discoverFromIoreg();
  void loadDump();
  void applyDiscovery(const QString& ioregText);
  void fillTable();
  void updateStatus();
  void buildKext();
  void setAll(int state);
  void enableWithDevices();
  void resetMap();
  void saveState() const;
  void loadState();
  static QString stateDir();
  UsbMap::Port* portAt(int row);

  QTableWidget* table;
  QLabel *status, *hint;
  QPushButton *btnDiscover, *btnDump, *btnBuild;
  QCheckBox* chkLegacy;
  UsbMap::ControllerList list;
  QString smbios;
  bool filling = false;
};

#endif
