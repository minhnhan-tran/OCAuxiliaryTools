#ifndef DLGEFIBUILDER_H
#define DLGEFIBUILDER_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>

#include "efibuilder/efibuilder.h"

// Wizard: Hardware Sniffer Report.json -> compatibility -> macOS version ->
// generated EFI (via the existing Method::generateEFI). Rules ported from
// lzhoang2801/OpCore-Simplify.
class dlgEfiBuilder : public QDialog {
  Q_OBJECT
 public:
  explicit dlgEfiBuilder(QWidget* parent = nullptr);

 private slots:
  void browse();
  void analyze();
  void build();
  void customize();
  void browseAcpi();
  void refresh();
  void showNextSteps(const QJsonObject& rep, const EfiBuilder::Plan& plan);

 private:
  EfiBuilder::AcpiInfo loadAcpi() const;
  QStringList databaseAml() const;
  void setResult(const QString& html);

  QLineEdit* editReport;
  QLineEdit* editAcpi;
  QCheckBox *chkQuiet, *chkHidePicker, *chkCanopy;
  QPushButton* btnBrowseAcpi;
  QPushButton *btnBrowse, *btnAnalyze, *btnBuild, *btnCustomize;
  QComboBox* cboMacos;
  QTextBrowser* view;

  QJsonObject fullReport;  // as loaded
  QJsonObject report;      // fullReport minus the GPUs the user did not pick
  QLabel* lblGpu;
  QComboBox* cboGpu;
  QList<EfiBuilder::GpuCombo> gpuCombos;
  EfiBuilder::Compatibility compat;
  bool analyzed = false;
  // User overrides (empty = use the automatic pick). Reset on every analyze.
  QString smbiosOverride;
  int audioOverride = 0;  // 0 = recommended default
  bool kextsOverridden = false;
  QStringList kextOverride;
};

#endif  // DLGEFIBUILDER_H
