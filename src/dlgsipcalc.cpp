#include "dlgsipcalc.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "mainwindow.h"

extern MainWindow* mw_one;

namespace {

struct Flag {
  quint32 bit;
  const char* name;
  const char* help;
};

// Names and bit positions: OpenCorePkg Include/Apple/IndustryStandard/AppleCsrConfig.h.
const Flag kFlags[] = {
    {0x001, "CSR_ALLOW_UNTRUSTED_KEXTS", "Load kexts that are not signed by Apple."},
    {0x002, "CSR_ALLOW_UNRESTRICTED_FS", "Write to protected system files and folders."},
    {0x004, "CSR_ALLOW_TASK_FOR_PID", "Let debuggers attach to any process."},
    {0x008, "CSR_ALLOW_KERNEL_DEBUGGER", "Allow kernel debugging."},
    {0x010, "CSR_ALLOW_APPLE_INTERNAL", "Apple-internal mode."},
    {0x020, "CSR_ALLOW_UNRESTRICTED_DTRACE", "Unrestricted DTrace."},
    {0x040, "CSR_ALLOW_UNRESTRICTED_NVRAM", "Write any NVRAM variable."},
    {0x080, "CSR_ALLOW_DEVICE_CONFIGURATION", "Allow device configuration changes."},
    {0x100, "CSR_ALLOW_ANY_RECOVERY_OS", "Boot any recovery OS."},
    {0x200, "CSR_ALLOW_UNAPPROVED_KEXTS", "Load kexts the user has not approved."},
    {0x400, "CSR_ALLOW_EXECUTABLE_POLICY_OVERRIDE", "Override the executable policy."},
    {0x800, "CSR_ALLOW_UNAUTHENTICATED_ROOT", "Boot an unsealed system volume (needed for root patches)."},
};
const quint32 kKnownMask = 0xFFF;

struct Preset {
  const char* label;
  quint32 value;
};
// 0x27F is OpenCore's own default for disabling SIP (Configuration.pdf, Misc > Security > CsrActiveConfig).
// 0x803 (Data 03080000) is what OpenCore Legacy Patcher writes for root patching
// (efi_builder/security.py): UNTRUSTED_KEXTS | UNRESTRICTED_FS | UNAUTHENTICATED_ROOT.
const Preset kPresets[] = {
    {"Enabled (0x0)", 0x0},
    {"OpenCore default, SIP off (0x27F)", 0x27F},
    {"OCLP root patching (0x803)", 0x803},
    {"All known flags (0xFFF)", 0xFFF},
};

}  // namespace

QString dlgSipCalc::toDataHex(quint32 v) {
  return QString("%1%2%3%4")
      .arg(v & 0xFF, 2, 16, QChar('0'))
      .arg((v >> 8) & 0xFF, 2, 16, QChar('0'))
      .arg((v >> 16) & 0xFF, 2, 16, QChar('0'))
      .arg((v >> 24) & 0xFF, 2, 16, QChar('0'))
      .toUpper();
}

bool dlgSipCalc::fromDataHex(const QString& s, quint32* v) {
  QString t = s;
  t.remove(' ');
  if (t.size() != 8) return false;
  bool ok = false;
  const quint32 raw = t.toUInt(&ok, 16);  // big-endian reading of the 4 bytes
  if (!ok) return false;
  *v = ((raw & 0xFF) << 24) | ((raw & 0xFF00) << 8) | ((raw >> 8) & 0xFF00) | (raw >> 24);
  return true;
}

bool dlgSipCalc::fromNumber(const QString& s, quint32* v) {
  QString t = s.trimmed();
  if (t.isEmpty()) return false;
  bool ok = false;
  quint32 n = 0;
  if (t.startsWith("0x", Qt::CaseInsensitive)) n = t.mid(2).toUInt(&ok, 16);
  else n = t.toUInt(&ok, 16);  // a bare value is read as hex, like Hackintool
  if (!ok) return false;
  *v = n;
  return true;
}

QString dlgSipCalc::flagNames(quint32 v) {
  QStringList l;
  for (const Flag& f : kFlags)
    if (v & f.bit) l << f.name;
  if (v & ~kKnownMask) l << QString("unknown bits 0x%1").arg(v & ~kKnownMask, 0, 16);
  return l.isEmpty() ? QStringLiteral("none (SIP fully enabled)") : l.join(", ");
}

dlgSipCalc::dlgSipCalc(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("SIP / csr-active-config calculator"));
  resize(620, 560);
  auto* top = new QVBoxLayout(this);

  auto* hint = new QLabel(tr("Tick the SIP protections to switch off. The result is the value for NVRAM > Add > "
                             "7C436110-AB2A-4BBB-A880-FE41995C9F82 > csr-active-config (type Data), or for "
                             "Misc > Security > CsrActiveConfig (number)."));
  hint->setWordWrap(true);
  top->addWidget(hint);

  auto* grp = new QGroupBox(tr("Flags"));
  auto* gl = new QVBoxLayout(grp);
  gl->setSpacing(6);
  for (const Flag& f : kFlags) {
    auto* cb = new QCheckBox(QString("0x%1   %2").arg(f.bit, 3, 16, QChar('0')).arg(f.name));
    cb->setToolTip(tr(f.help));
    cb->setProperty("bit", f.bit);
    gl->addWidget(cb);
    boxes_ << cb;
    connect(cb, &QCheckBox::toggled, this, [this, cb](bool on) {
      if (busy_) return;
      const quint32 bit = cb->property("bit").toUInt();
      setValue(on ? (value_ | bit) : (value_ & ~bit), cb);
    });
  }
  top->addWidget(grp);

  auto* pr = new QHBoxLayout;
  pr->addWidget(new QLabel(tr("Presets:")));
  for (const Preset& p : kPresets) {
    auto* b = new QPushButton(tr(p.label));
    const quint32 val = p.value;
    connect(b, &QPushButton::clicked, this, [this, val] { setValue(val); });
    pr->addWidget(b);
  }
  pr->addStretch();
  top->addLayout(pr);

  auto* g2 = new QGridLayout;
  g2->setHorizontalSpacing(10);
  g2->addWidget(new QLabel(tr("Number (hex)")), 0, 0);
  editNum_ = new QLineEdit;
  g2->addWidget(editNum_, 0, 1);
  g2->addWidget(new QLabel(tr("NVRAM Data (4 bytes)")), 1, 0);
  editData_ = new QLineEdit;
  g2->addWidget(editData_, 1, 1);
  g2->setColumnStretch(1, 1);
  top->addLayout(g2);

  lblSummary_ = new QLabel;
  lblSummary_->setWordWrap(true);
  top->addWidget(lblSummary_);

  connect(editNum_, &QLineEdit::textEdited, this, [this](const QString& s) {
    quint32 v;
    if (fromNumber(s, &v)) setValue(v, editNum_);
  });
  connect(editData_, &QLineEdit::textEdited, this, [this](const QString& s) {
    quint32 v;
    if (fromDataHex(s, &v)) setValue(v, editData_);
  });

  auto* bb = new QDialogButtonBox;
  auto* copy = bb->addButton(tr("Copy Data"), QDialogButtonBox::ActionRole);
  QPushButton* ap = mw_one ? bb->addButton(tr("Apply to open config"), QDialogButtonBox::ActionRole) : nullptr;
  bb->addButton(QDialogButtonBox::Close);
  connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(toDataHex(value_)); });
  if (ap) connect(ap, &QPushButton::clicked, this, &dlgSipCalc::apply);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::close);
  top->addWidget(bb);

  setValue(0x0);
}

void dlgSipCalc::setValue(quint32 v, QWidget* source) {
  busy_ = true;
  value_ = v;
  for (QCheckBox* cb : boxes_) {
    if (cb == source) continue;
    cb->setChecked((v & cb->property("bit").toUInt()) != 0);
  }
  if (source != editNum_) editNum_->setText(QString("0x%1").arg(v, 0, 16).toUpper().replace("0X", "0x"));
  if (source != editData_) editData_->setText(toDataHex(v));
  lblSummary_->setText(tr("Active flags: %1").arg(flagNames(v)));
  busy_ = false;
}

void dlgSipCalc::apply() {
  if (!mw_one) return;
  if (QMessageBox::question(this, windowTitle(),
                            tr("Set csr-active-config to %1 (Data %2) in the open config's NVRAM > Add?")
                                .arg(editNum_->text(), toDataHex(value_))) != QMessageBox::Yes)
    return;
  if (!mw_one->setNvramAddValue("7C436110-AB2A-4BBB-A880-FE41995C9F82", "csr-active-config", "Data",
                                toDataHex(value_)))
    QMessageBox::warning(this, windowTitle(), tr("Could not change the NVRAM table."));
}
