#ifndef DLGMACOSINSTALLER_H
#define DLGMACOSINSTALLER_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

#include "macosinstaller/macosinstaller.h"

// Tools > Create macOS Installation: make a bootable macOS installer USB with Apple's
// createinstallmedia, the same approach as Dortania's OpenCore Legacy Patcher. The
// dialog only prepares the command; it runs in Terminal after the user confirms.
class dlgMacOSInstaller : public QDialog {
  Q_OBJECT
 public:
  explicit dlgMacOSInstaller(QWidget* parent = nullptr);

 private:
  void refreshInstallers();
  void refreshDisks();
  void fetchAppleList();
  void browseInstaller();
  void browseEfi();
  void downloadSelected();
  void updateCommand();
  void createUsb();
  QString currentInstaller() const;
  bool currentDisk(MacInstaller::Disk* d) const;

  QComboBox *cboInstaller, *cboApple, *cboDisk;
  QLabel *lblInstaller, *lblDisk, *lblEfi, *lblApple;
  QCheckBox* chkEfi;
  QLineEdit* editEfi;
  QPlainTextEdit* preview;
  QPushButton *btnCreate, *btnCopy, *btnDownload;
  QString extraInstaller;  // chosen with Browse
  QString command;         // the command the buttons use; "" when something is missing
  QList<MacInstaller::AppleInstaller> appleList;
};

#endif
