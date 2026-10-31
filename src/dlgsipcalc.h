#ifndef DLGSIPCALC_H
#define DLGSIPCALC_H

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>

// SIP / csr-active-config calculator. Flag bits are the CSR_ALLOW_* values from
// OpenCorePkg's AppleCsrConfig.h. The NVRAM value is a 4-byte little-endian Data.
class dlgSipCalc : public QDialog {
  Q_OBJECT
 public:
  explicit dlgSipCalc(QWidget* parent = nullptr);

  static QString toDataHex(quint32 v);                       // 0x0A43 -> "430A0000"
  static bool fromDataHex(const QString& s, quint32* v);     // exactly 4 bytes
  static bool fromNumber(const QString& s, quint32* v);      // "0x0A43" / "A43"
  static QString flagNames(quint32 v);                       // active flags, comma separated

 private:
  void setValue(quint32 v, QWidget* source = nullptr);
  void apply();

  quint32 value_ = 0;  // raw value, so bits this dialog does not know survive toggling
  QList<QCheckBox*> boxes_;
  QLineEdit* editNum_ = nullptr;
  QLineEdit* editData_ = nullptr;
  QLabel* lblSummary_ = nullptr;
  bool busy_ = false;
};

#endif  // DLGSIPCALC_H
