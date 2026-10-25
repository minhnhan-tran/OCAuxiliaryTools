// EFI builder wizard. Rules/datasets ported from lzhoang2801/OpCore-Simplify
// (BSD-3-Clause).
#include "dlgefibuilder.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QHeaderView>
#include <QListWidget>
#include <QProcess>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QProgressDialog>

#include "Method.h"
#include "mainwindow.h"
#include "plistparser.h"
#include "plistserializer.h"
#include "efibuilder/kextfetch.h"

extern MainWindow* mw_one;
extern Method* mymethod;
extern QString strAppExePath;

namespace {

QString esc(const QString& s) { return s.toHtmlEscaped(); }

// Fresh serial/MLB/UUID for the chosen model using the bundled macserial.
bool applySerials(QVariantMap* config, const QString& model, const QString& dbDir) {
#if defined(Q_OS_WIN)
  const QString exe = dbDir + "win/macserial.exe";
#elif defined(Q_OS_MAC)
  const QString exe = dbDir + "mac/macserial";
#else
  const QString exe = dbDir + "linux/macserial";
#endif
  if (!QFile::exists(exe)) return false;
  QProcess p;
  p.start(exe, {"-m", model, "-n", "1"});
  if (!p.waitForFinished(8000)) return false;
  const QStringList parts = QString::fromUtf8(p.readAllStandardOutput()).trimmed().split('|');
  if (parts.size() < 2) return false;
  QVariantMap pi = config->value("PlatformInfo").toMap();
  QVariantMap gen = pi.value("Generic").toMap();
  gen["SystemSerialNumber"] = parts[0].trimmed();
  gen["MLB"] = parts[1].trimmed();
  gen["SystemUUID"] = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
  pi["Generic"] = gen;
  config->insert("PlatformInfo", pi);
  return true;
}

}  // namespace

dlgEfiBuilder::dlgEfiBuilder(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("EFI Builder (OpCore Simplify)"));
  resize(760, 620);

  editReport = new QLineEdit;
  editReport->setPlaceholderText(tr("Path to Hardware Sniffer Report.json"));
  btnBrowse = new QPushButton(tr("Browse..."));
  btnAnalyze = new QPushButton(tr("Analyze"));
  editAcpi = new QLineEdit;
  editAcpi->setPlaceholderText(tr("ACPI folder next to Report.json (auto-detected, optional)"));
  btnBrowseAcpi = new QPushButton(tr("Browse ACPI..."));
  cboMacos = new QComboBox;
  btnCustomize = new QPushButton(tr("Customize SMBIOS / Kexts..."));
  btnCustomize->setEnabled(false);
  btnBuild = new QPushButton(tr("Build EFI"));
  btnBuild->setEnabled(false);
  view = new QTextBrowser;
  view->setOpenExternalLinks(true);

  QHBoxLayout* top = new QHBoxLayout;
  top->addWidget(editReport, 1);
  top->addWidget(btnBrowse);
  top->addWidget(btnAnalyze);

  QHBoxLayout* acpiRow = new QHBoxLayout;
  acpiRow->addWidget(editAcpi, 1);
  acpiRow->addWidget(btnBrowseAcpi);

  // Optional "finalize for daily use" edits (Dortania post-install: cosmetic).
  chkQuiet = new QCheckBox(tr("Quiet boot (remove -v / debug)"));
  chkHidePicker = new QCheckBox(tr("Hide boot picker"));
  chkCanopy = new QCheckBox(tr("Graphical picker (OpenCanopy)"));
  chkQuiet->setToolTip(tr("Post-install, once everything works. Re-add -v to debug a boot problem."));
  chkHidePicker->setToolTip(tr("Hold Esc or Space while booting to show the picker again."));
  chkCanopy->setToolTip(tr("Needs OpenCanopy.efi and the Resources folder in the Database."));
  QHBoxLayout* finRow = new QHBoxLayout;
  finRow->addWidget(new QLabel(tr("Finalize:")));
  finRow->addWidget(chkQuiet);
  finRow->addWidget(chkHidePicker);
  finRow->addWidget(chkCanopy);
  finRow->addStretch(1);

  // GPU combination (only shown when the report has several GPUs).
  lblGpu = new QLabel(tr("GPU:"));
  cboGpu = new QComboBox;
  cboGpu->setToolTip(tr("GPUs you do not pick are disabled with disable-gpu. Several active GPUs can cause kext conflicts in macOS."));
  lblGpu->hide();
  cboGpu->hide();
  QHBoxLayout* gpuRow = new QHBoxLayout;
  gpuRow->addWidget(lblGpu);
  gpuRow->addWidget(cboGpu, 1);

  QHBoxLayout* bottom = new QHBoxLayout;
  bottom->addWidget(new QLabel(tr("Target macOS:")));
  bottom->addWidget(cboMacos, 1);
  bottom->addWidget(btnCustomize);
  bottom->addWidget(btnBuild);

  QVBoxLayout* lay = new QVBoxLayout(this);
  lay->addWidget(new QLabel(
      tr("Generate the report with <a href=\"https://github.com/lzhoang2801/"
         "Hardware-Sniffer\">Hardware Sniffer</a> on the target PC, then load "
         "it here.")));
  lay->addLayout(top);
  lay->addLayout(acpiRow);
  lay->addWidget(view, 1);
  lay->addLayout(gpuRow);
  lay->addLayout(finRow);
  lay->addLayout(bottom);

  connect(btnBrowse, &QPushButton::clicked, this, &dlgEfiBuilder::browse);
  connect(btnAnalyze, &QPushButton::clicked, this, &dlgEfiBuilder::analyze);
  connect(btnBrowseAcpi, &QPushButton::clicked, this, &dlgEfiBuilder::browseAcpi);
  connect(editAcpi, &QLineEdit::returnPressed, this, &dlgEfiBuilder::analyze);
  connect(editReport, &QLineEdit::returnPressed, this, &dlgEfiBuilder::analyze);
  connect(btnBuild, &QPushButton::clicked, this, &dlgEfiBuilder::build);
  connect(cboGpu, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { if (!fullReport.isEmpty()) refresh(); });
  connect(btnCustomize, &QPushButton::clicked, this, &dlgEfiBuilder::customize);
  // A different macOS target changes which models/kexts are valid.
  connect(cboMacos, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    smbiosOverride.clear();
    audioOverride = 0;
    kextsOverridden = false;
    kextOverride.clear();
  });
  auto invalidate = [this] {
    analyzed = false;
    btnBuild->setEnabled(false);
    btnCustomize->setEnabled(false);
  };
  connect(editReport, &QLineEdit::textChanged, this, invalidate);
  connect(editAcpi, &QLineEdit::textChanged, this, invalidate);
}

void dlgEfiBuilder::browse() {
  const QString f = QFileDialog::getOpenFileName(
      this, tr("Select hardware report"), QDir::homePath(),
      tr("JSON (*.json);;All files (*.*)"));
  if (f.isEmpty()) return;
  editAcpi->clear();  // a folder picked for another report must not carry over
  editReport->setText(f);
  analyze();
}

void dlgEfiBuilder::setResult(const QString& html) { view->setHtml(html); }

void dlgEfiBuilder::browseAcpi() {
  const QString start = editAcpi->text().isEmpty()
                            ? QFileInfo(editReport->text().trimmed()).absolutePath()
                            : editAcpi->text();
  const QString d = QFileDialog::getExistingDirectory(this, tr("Select the ACPI folder"), start);
  if (d.isEmpty()) return;
  editAcpi->setText(d);
  if (!editReport->text().trimmed().isEmpty()) analyze();
}

// File names of the SSDTs the Database can supply.
QStringList dlgEfiBuilder::databaseAml() const {
  QStringList out;
  for (const QFileInfo& fi : QDir(mw_one->userDataBaseDir + "EFI/OC/ACPI").entryInfoList(QDir::Files))
    if (fi.fileName().endsWith(".aml", Qt::CaseInsensitive)) out << fi.fileName();
  return out;
}

// Reads the ACPI folder: the one typed/browsed, else "ACPI" beside Report.json.
EfiBuilder::AcpiInfo dlgEfiBuilder::loadAcpi() const {
  QString dir = editAcpi->text().trimmed();
  if (dir.isEmpty()) {
    const QFileInfo rep(editReport->text().trimmed());
    const QString guess = rep.absoluteDir().filePath("ACPI");
    if (QDir(guess).exists()) dir = guess;
  }
  return EfiBuilder::readAcpiFolder(dir);
}

void dlgEfiBuilder::analyze() {
  analyzed = false;
  btnBuild->setEnabled(false);
  btnCustomize->setEnabled(false);
  cboMacos->clear();
  smbiosOverride.clear();
    audioOverride = 0;
  kextsOverridden = false;
  kextOverride.clear();

  gpuCombos.clear();
  { QSignalBlocker b(cboGpu); cboGpu->clear(); }
  lblGpu->hide();
  cboGpu->hide();
  fullReport = QJsonObject();
  QStringList errors;
  if (!EfiBuilder::loadReport(editReport->text().trimmed(), &fullReport, &errors)) {
    QString h = "<h3>" + esc(tr("Invalid report")) + "</h3><ul>";
    for (const QString& e : errors) h += "<li>" + esc(e) + "</li>";
    setResult(h + "</ul>");
    return;
  }

  // Several GPUs: let the user pick a combination, like OpCore Simplify.
  report = fullReport;
  compat = EfiBuilder::checkCompatibility(fullReport);
  gpuCombos = EfiBuilder::gpuCombinations(fullReport, compat);
  if (!gpuCombos.isEmpty()) {
    QSignalBlocker b(cboGpu);
    int best = 0;
    for (int i = 0; i < gpuCombos.size(); ++i) {
      const auto& gc = gpuCombos[i];
      const QString rg = gc.range.supported()
          ? tr("up to %1").arg(EfiBuilder::macosName(EfiBuilder::darwinMajor(gc.range.maxV)))
          : tr("unsupported");
      cboGpu->addItem(gc.names.join(" + ") + "   (" + rg + ")");
      if (gc.names.size() > gpuCombos[best].names.size()) best = i;  // default: keep as many as are valid
    }
    cboGpu->setCurrentIndex(best);
    lblGpu->show();
    cboGpu->show();
  }
  refresh();
}

void dlgEfiBuilder::refresh() {
  analyzed = false;
  btnBuild->setEnabled(false);
  btnCustomize->setEnabled(false);
  cboMacos->clear();
  smbiosOverride.clear();
  audioOverride = 0;
  kextsOverridden = false;
  kextOverride.clear();
  report = fullReport;
  const int gi = cboGpu->currentIndex();
  if (!gpuCombos.isEmpty() && gi >= 0 && gi < gpuCombos.size())
    report = EfiBuilder::withGpus(fullReport, gpuCombos[gi].names);

  compat = EfiBuilder::checkCompatibility(report);
  const QJsonObject cpu = report["CPU"].toObject();
  QString h = "<h3>" + esc(cpu["Processor Name"].toString()) + " (" +
              esc(cpu["Codename"].toString()) + ")</h3>";
  h += "<table cellpadding=3 border=0><tr><th align=left>" + esc(tr("Type")) +
       "</th><th align=left>" + esc(tr("Device")) + "</th><th align=left>" +
       esc(tr("macOS range")) + "</th><th align=left>" + esc(tr("Note")) + "</th></tr>";
  for (const auto& d : compat.devices) {
    QString range =
        d.range.supported()
            ? esc(EfiBuilder::macosName(EfiBuilder::darwinMajor(d.range.minV))) + " &rarr; " +
                  esc(EfiBuilder::macosName(EfiBuilder::darwinMajor(d.range.maxV)))
            : "<span style='color:#d32f2f'>" + esc(tr("Unsupported")) + "</span>";
    h += "<tr><td>" + esc(d.kind) + "</td><td>" + esc(d.name) + "</td><td>" + range +
         "</td><td>" + esc(d.note) + "</td></tr>";
  }
  h += "</table>";
  for (const QString& w : compat.warnings)
    h += "<p style='color:#d32f2f'>&#9888; " + esc(w) + "</p>";

  // ACPI folder (tables dumped next to the report), like OpCore Simplify.
  {
    const EfiBuilder::AcpiInfo ai = loadAcpi();
    h += "<h4>" + esc(tr("ACPI tables")) + "</h4>";
    if (!ai.found) {
      h += "<p>" + esc(tr("No ACPI folder found next to the report. SSDTs will come from the base config. "
                          "Use \"Browse ACPI...\" to select it.")) + "</p>";
    } else {
      h += "<p>" + esc(ai.dir) + ": " + esc(tr("%1 table(s)").arg(ai.tables.size())) +
           (ai.hasDsdt ? "" : " - <span style='color:#d32f2f'>" + esc(tr("no DSDT")) + "</span>") + "</p>";
      for (const QString& w : ai.warnings)
        h += "<p style='color:#d32f2f'>&#9888; " + esc(w) + "</p>";
    }
  }

  const auto choices = EfiBuilder::macosChoices(compat);
  for (const auto& c : choices) cboMacos->addItem(c.first, c.second);
  if (choices.isEmpty()) {
    h += "<p><b>" + esc(tr("No macOS version is supported by this hardware.")) + "</b></p>";
  } else {
    h += "<p><b>" + esc(tr("Native support")) + ":</b> " +
         esc(choices.last().first) + " &rarr; " + esc(choices.first().first) + "</p>";
    analyzed = true;
    btnBuild->setEnabled(true);
    btnCustomize->setEnabled(true);
  }
  setResult(h);
}

void dlgEfiBuilder::build() {
  if (!analyzed || cboMacos->currentIndex() < 0) return;

  const QString target = QString("%1.99.99").arg(cboMacos->currentData().toInt());
  EfiBuilder::Plan plan = EfiBuilder::makePlan(report, compat, target);
  EfiBuilder::overrideSmbios(&plan, smbiosOverride);
  EfiBuilder::overrideAudio(&plan, audioOverride);
  EfiBuilder::planAcpi(report, loadAcpi(), &plan, databaseAml());
  const QStringList defaultKexts = EfiBuilder::resolveKexts(plan.kexts, target);
  QStringList disableKexts;
  if (kextsOverridden) {
    for (const QString& n : defaultKexts)
      if (!kextOverride.contains(n)) disableKexts << n;
    EfiBuilder::overrideKexts(&plan, kextOverride);
  } else {
    EfiBuilder::overrideKexts(&plan, defaultKexts);
  }

  const QString dbDir = mw_one->userDataBaseDir;
  QString tplPath = dbDir + "BaseConfigs/" + plan.templateName + ".plist";
  if (!QFile::exists(tplPath)) tplPath = dbDir + "BaseConfigs/SampleCustom.plist";
  QFile tpl(tplPath);
  if (!tpl.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, "", tr("Cannot open base config: %1").arg(tplPath));
    return;
  }
  QVariantMap config = PListParser::parsePList(&tpl).toMap();
  tpl.close();
  if (config.isEmpty()) {
    QMessageBox::warning(this, "", tr("Base config is not a valid plist."));
    return;
  }

  const QString kextDir = dbDir + "EFI/OC/Kexts";
  auto scanKexts = [&kextDir]() {
    QStringList out;
    for (const QFileInfo& fi : QDir(kextDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const QString n = fi.fileName();
      if (n.endsWith(".kext")) out << n.left(n.size() - 5);
    }
    return out;
  };
  QStringList available = scanKexts();

  // Like OpCore Simplify: fetch every chosen kext the Database doesn't have.
  QStringList toFetch;
  for (const QString& n : plan.kexts)
    if (!available.contains(n)) toFetch << n;
  EfiBuilder::FetchResult fetched;
  if (!toFetch.isEmpty()) {
    EfiBuilder::FetchOptions fo;
    auto* pref = mw_one->myDlgPreference->ui;
    if (pref->chkProxy->isChecked()) {
      fo.proxyHost = pref->txtHostName->text().trimmed();
      fo.proxyPort = pref->txtPort->text().trimmed().toInt();
    }
    if (pref->rbtnToken->isChecked()) fo.token = pref->editToken->text().trimmed();
#ifdef Q_OS_WIN
    fo.unzipProgram = strAppExePath + "/unzip.exe";
#endif
    QProgressDialog prog(tr("Downloading kexts..."), tr("Cancel"), 0, toFetch.size(), this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(0);
    int step = 0;
    prog.setValue(0);
    fetched = EfiBuilder::fetchKexts(
        toFetch, cboMacos->currentData().toInt(), kextDir, fo,
        [&](const QString& status) {
          prog.setLabelText(status);
          prog.setValue(step++);
          QApplication::processEvents();
          return !prog.wasCanceled();
        });
    prog.setValue(toFetch.size());
    available = scanKexts();
  }

  QStringList missing;
  config = EfiBuilder::applyPlan(config, plan, available, &missing, disableKexts, kextDir);
  // GPUs the user did not pick get disable-gpu, so macOS ignores them.
  const int gpusOff = EfiBuilder::disableGpus(&config, fullReport, report);
  if (gpusOff > 0) plan.notes << tr("%1 GPU(s) not selected: disabled with disable-gpu in DeviceProperties.").arg(gpusOff);
  // Keep the config and the EFI folder to what this build uses: no disabled sample rows.
  EfiBuilder::pruneUnused(&config);
  const bool serials = applySerials(&config, plan.smbios, dbDir);

  EfiBuilder::Finalize fin;
  fin.quietBoot = chkQuiet->isChecked();
  fin.hidePicker = chkHidePicker->isChecked();
  fin.openCanopy = chkCanopy->isChecked();
  if (fin.any()) {
    QStringList drivers;
    for (const QFileInfo& fi : QDir(dbDir + "EFI/OC/Drivers").entryInfoList(QDir::Files))
      drivers << fi.fileName();
    const bool resOk = QDir(dbDir + "EFI/OC/Resources").exists();
    EfiBuilder::finalizeConfig(&config, fin, drivers, resOk, &plan.notes);
  }

  const QString tmp = QDir::homePath() + "/tempocat/";
  QDir().mkpath(tmp);
  const QString file = tmp + "OpCoreSimplify.plist";
  PListSerializer::toPList(config, file);

  QString summary = tr("Base config: %1\nSMBIOS: %2\nKexts: %3").arg(plan.templateName, plan.smbios, plan.kexts.join(", "));
  if (!fetched.ok.isEmpty())
    summary += "\n\n" + tr("Downloaded: %1").arg(fetched.ok.join(", "));
  if (!missing.isEmpty()) {
    summary += "\n\n" + tr("Not available - add manually: %1").arg(missing.join(", "));
    for (auto it = fetched.failed.constBegin(); it != fetched.failed.constEnd(); ++it)
      summary += "\n  " + it.key() + ": " + it.value();
  }
  if (!serials)
    summary += "\n\n" + tr("macserial not found: set Serial/MLB/UUID yourself.");
  for (const QString& n : plan.notes) summary += "\n- " + n;
  if (QMessageBox::question(this, tr("Generate EFI"),
                            summary + "\n\n" + tr("Generate the EFI folder on the Desktop now?")) !=
      QMessageBox::Yes)
    return;

  accept();
  mw_one->isGetEFI = true;
  mw_one->openFile(file);
  mw_one->isGetEFI = false;
  mymethod->generateEFI("OpCoreSimplify.plist");
  showNextSteps(report, plan);
}

// Post-install checklist (Dortania OpenCore Post-Install guide), tailored to this build.
void dlgEfiBuilder::showNextSteps(const QJsonObject& rep, const EfiBuilder::Plan& plan) {
  const QList<EfiBuilder::NextStep> steps = EfiBuilder::nextSteps(rep, plan);
  QDialog dlg(mw_one);
  dlg.setWindowTitle(tr("Next steps - post-install"));
  dlg.resize(620, 480);
  QVBoxLayout* lay = new QVBoxLayout(&dlg);
  auto esc = [](const QString& s) { return s.toHtmlEscaped(); };
  QString h = "<p>" + esc(tr("Your EFI is on the Desktop. Boot it, then work through these steps:")) + "</p><ol>";
  for (const EfiBuilder::NextStep& s : steps) {
    h += "<li><b>" + esc(s.title) + "</b><br>" + esc(s.text);
    if (QUrl(s.url).scheme() == "https")  // only open links the data file marks as https
      h += " <a href=\"" + esc(s.url) + "\">" + esc(tr("Guide")) + "</a>";
    h += "</li>";
  }
  h += "</ol><p><a href=\"" + esc(EfiBuilder::postInstallGuideUrl()) + "\">" +
       esc(tr("Open the full OpenCore Post-Install guide")) + "</a></p>";
  QTextBrowser* tb = new QTextBrowser;
  tb->setOpenExternalLinks(true);
  tb->setHtml(h);
  lay->addWidget(tb);
  QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  lay->addWidget(bb);
  dlg.exec();
}

// Customization dialog, modelled on OpCore-Simplify's SMBIOS and kext menus.
void dlgEfiBuilder::customize() {
  if (!analyzed || cboMacos->currentIndex() < 0) return;
  const QString target = QString("%1.99.99").arg(cboMacos->currentData().toInt());
  const EfiBuilder::Plan autoPlan = EfiBuilder::makePlan(report, compat, target);
  const QString currentModel = smbiosOverride.isEmpty() ? autoPlan.smbios : smbiosOverride;
  const QStringList currentKexts =
      kextsOverridden ? kextOverride : EfiBuilder::resolveKexts(autoPlan.kexts, target);

  QStringList database;
  for (const QFileInfo& fi : QDir(mw_one->userDataBaseDir + "EFI/OC/Kexts")
                                 .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
    if (fi.fileName().endsWith(".kext")) database << fi.fileName().left(fi.fileName().size() - 5);

  QDialog dlg(this);
  dlg.setWindowTitle(tr("Customize SMBIOS / Kexts"));
  dlg.resize(820, 560);
  QTabWidget* tabs = new QTabWidget;

  // ---- SMBIOS tab
  QWidget* smPage = new QWidget;
  QVBoxLayout* smLay = new QVBoxLayout(smPage);
  QCheckBox* showAll = new QCheckBox(tr("Show all models (including unsupported / other platform)"));
  QTableWidget* smTable = new QTableWidget;
  smTable->setColumnCount(4);
  smTable->setHorizontalHeaderLabels({tr("Model"), tr("CPU"), tr("GPU"), tr("macOS support")});
  smTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  smTable->setSelectionMode(QAbstractItemView::SingleSelection);
  smTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  smTable->horizontalHeader()->setStretchLastSection(true);
  smTable->verticalHeader()->hide();
  smLay->addWidget(showAll);
  smLay->addWidget(smTable, 1);
  smLay->addWidget(new QLabel(tr("The auto-picked model is %1. Changing it keeps the same base "
                                 "config; check Platform properties afterwards.").arg(autoPlan.smbios)));

  const QList<EfiBuilder::SmbiosModel> models = EfiBuilder::smbiosModels(report, target);
  auto fillModels = [&]() {
    QString chosen = currentModel;
    const auto prev = smTable->selectedItems();
    if (!prev.isEmpty()) chosen = prev.first()->data(Qt::UserRole).toString();
    smTable->setRowCount(0);
    for (const auto& m : models) {
      const bool keep = m.name == chosen || m.name == autoPlan.smbios;
      if (!showAll->isChecked() && !keep && !(m.supported && m.platformMatch)) continue;
      const int row = smTable->rowCount();
      smTable->insertRow(row);
      QString range = EfiBuilder::macosName(EfiBuilder::darwinMajor(m.minV)) + " -> " +
                      EfiBuilder::macosName(EfiBuilder::darwinMajor(m.maxV));
      QStringList cols{m.name + (m.name == autoPlan.smbios ? tr("  (auto)") : QString()), m.cpu,
                       m.gpu, range};
      for (int c = 0; c < 4; ++c) {
        QTableWidgetItem* it = new QTableWidgetItem(cols[c]);
        it->setData(Qt::UserRole, m.name);
        if (!m.supported) it->setForeground(Qt::gray);
        smTable->setItem(row, c, it);
      }
      if (m.name == chosen) smTable->selectRow(row);
    }
    smTable->resizeColumnsToContents();
  };
  fillModels();
  connect(showAll, &QCheckBox::toggled, &dlg, [&](bool) { fillModels(); });
  tabs->addTab(smPage, tr("SMBIOS"));

  // ---- Kexts tab
  QWidget* kxPage = new QWidget;
  QVBoxLayout* kxLay = new QVBoxLayout(kxPage);
  QCheckBox* showIncompat = new QCheckBox(tr("Show kexts that are not compatible with the target macOS"));
  QListWidget* kxList = new QListWidget;
  kxLay->addWidget(showIncompat);
  kxLay->addWidget(kxList, 1);
  kxLay->addWidget(new QLabel(tr("Kexts marked [download] are fetched automatically when you build the EFI "
                                 "(Dortania build repo, direct URL or GitHub releases).")));
  const QList<EfiBuilder::KextInfo> catalog = EfiBuilder::kextCatalog(target, database);
  auto fillKexts = [&]() {
    // Preserve in-progress edits when the filter toggles.
    QStringList checked;
    for (int i = 0; i < kxList->count(); ++i)
      if (kxList->item(i)->checkState() == Qt::Checked)
        checked << kxList->item(i)->data(Qt::UserRole).toString();
    const QStringList on = kxList->count() ? checked : currentKexts;
    kxList->clear();
    QString lastCat;
    for (const auto& k : catalog) {
      if (!k.compatible && !showIncompat->isChecked() && !on.contains(k.name)) continue;
      QString label = k.name;
      if (k.required) label += tr("  [required]");
      if (!k.inDatabase) label += tr("  [download]");
      if (!k.compatible) label += tr("  [not compatible]");
      if (!k.conflictGroup.isEmpty()) label += tr("  [exclusive: %1]").arg(k.conflictGroup);
      if (!k.category.isEmpty()) label = "[" + k.category + "] " + label;
      QListWidgetItem* it = new QListWidgetItem(label);
      it->setData(Qt::UserRole, k.name);
      it->setToolTip(k.description);
      it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
      it->setCheckState(on.contains(k.name) || k.required ? Qt::Checked : Qt::Unchecked);
      if (k.required) it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
      if (!k.compatible) it->setForeground(Qt::gray);
      kxList->addItem(it);
    }
  };
  fillKexts();
  connect(showIncompat, &QCheckBox::toggled, &dlg, [&](bool) { fillKexts(); });
  // Enforce one-per-conflict-group as the user ticks.
  connect(kxList, &QListWidget::itemChanged, &dlg, [&](QListWidgetItem* changed) {
    if (changed->checkState() != Qt::Checked) return;
    QString group;
    for (const auto& k : catalog)
      if (k.name == changed->data(Qt::UserRole).toString()) group = k.conflictGroup;
    if (group.isEmpty()) return;
    QSignalBlocker block(kxList);
    for (int i = 0; i < kxList->count(); ++i) {
      QListWidgetItem* o = kxList->item(i);
      if (o == changed || o->checkState() != Qt::Checked) continue;
      for (const auto& k : catalog)
        if (k.name == o->data(Qt::UserRole).toString() && k.conflictGroup == group)
          o->setCheckState(Qt::Unchecked);
    }
  });
  tabs->addTab(kxPage, tr("Kexts"));

  // ---- Audio tab (AppleALC codec layout)
  const EfiBuilder::AudioInfo audio = EfiBuilder::audioInfo(report);
  QTableWidget* auTable = new QTableWidget;
  if (!audio.layouts.isEmpty()) {
    QWidget* auPage = new QWidget;
    QVBoxLayout* auLay = new QVBoxLayout(auPage);
    auLay->addWidget(new QLabel(tr("Codec: %1 (%2). The recommended layout is highlighted; the "
                                   "default may not be optimal, so try others if audio fails.")
                                    .arg(audio.codecName, audio.codecId)));
    auTable->setColumnCount(2);
    auTable->setHorizontalHeaderLabels({tr("ID"), tr("Comment")});
    auTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    auTable->setSelectionMode(QAbstractItemView::SingleSelection);
    auTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auTable->horizontalHeader()->setStretchLastSection(true);
    auTable->verticalHeader()->hide();
    const int current = audioOverride > 0 ? audioOverride : audio.defaultId;
    for (const EfiBuilder::CodecLayout& l : audio.layouts) {
      const int row = auTable->rowCount();
      auTable->insertRow(row);
      QTableWidgetItem* a = new QTableWidgetItem(QString::number(l.id));
      QTableWidgetItem* b = new QTableWidgetItem(
          l.comment + (l.id == audio.defaultId ? tr("  (default)") : QString()));
      a->setData(Qt::UserRole, l.id);
      if (l.recommended) { QFont f = a->font(); f.setBold(true); a->setFont(f); b->setFont(f); }
      auTable->setItem(row, 0, a);
      auTable->setItem(row, 1, b);
      if (l.id == current) auTable->selectRow(row);
    }
    auTable->resizeColumnToContents(0);
    auLay->addWidget(auTable, 1);
    tabs->addTab(auPage, tr("Audio"));
  }

  QPushButton* reset = new QPushButton(tr("Reset to automatic"));
  QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  box->addButton(reset, QDialogButtonBox::ResetRole);
  QVBoxLayout* top = new QVBoxLayout(&dlg);
  top->addWidget(tabs, 1);
  top->addWidget(box);
  connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(reset, &QPushButton::clicked, &dlg, [&] {
    smbiosOverride.clear();
    audioOverride = 0;
    kextsOverridden = false;
    kextOverride.clear();
    dlg.done(2);
  });

  const int rc = dlg.exec();
  if (rc == 2) return;  // reset
  if (rc != QDialog::Accepted) return;

  // SMBIOS
  const auto sel = smTable->selectedItems();
  if (!sel.isEmpty()) {
    const QString name = sel.first()->data(Qt::UserRole).toString();
    smbiosOverride = (name == autoPlan.smbios) ? QString() : name;
  }
  // Audio layout
  if (auTable->rowCount() > 0) {
    const auto asel = auTable->selectedItems();
    if (!asel.isEmpty()) {
      const int id = asel.first()->data(Qt::UserRole).toInt();
      audioOverride = (id == audio.defaultId) ? 0 : id;
    }
  }
  // Kexts: the dialog may hide incompatible/unchecked entries, so re-derive
  // from what is checked there, keeping previous picks that were filtered out.
  QStringList picked;
  QSet<QString> shown;
  for (int i = 0; i < kxList->count(); ++i) {
    const QString n = kxList->item(i)->data(Qt::UserRole).toString();
    shown.insert(n);
    if (kxList->item(i)->checkState() == Qt::Checked) picked << n;
  }
  for (const QString& n : currentKexts)
    if (!shown.contains(n)) picked << n;
  picked.removeDuplicates();
  kextOverride = picked;
  kextsOverridden = true;
  QMessageBox::information(
      this, tr("Customize"),
      tr("SMBIOS: %1\nAudio layout: %2\nKexts (%3): %4")
          .arg(smbiosOverride.isEmpty() ? autoPlan.smbios + tr(" (auto)") : smbiosOverride)
          .arg(audio.layouts.isEmpty() ? tr("none")
                                       : QString::number(audioOverride > 0 ? audioOverride : audio.defaultId))
          .arg(picked.size())
          .arg(picked.join(", ")));
}
