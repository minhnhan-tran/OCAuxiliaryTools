#include "dlgpostcheck.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QScrollArea>
#include <QSplitter>
#include <QUrl>

#include "efibuilder/efibuilder.h"

using namespace PostCheck;

namespace {
QString statusText(Status s) {
  return s == Pass ? QObject::tr("OK") : s == Fail ? QObject::tr("Problem") : s == Skipped ? QObject::tr("Skipped") : QObject::tr("Unknown");
}
}  // namespace

dlgPostCheck::dlgPostCheck(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Post-Install Check"));
  resize(900, 600);

  summary = new QLabel;
  summary->setWordWrap(true);
  btnRun = new QPushButton(tr("Run checks"));
  btnCopy = new QPushButton(tr("Copy report"));
  btnCopy->setEnabled(false);

  tree = new QTreeWidget;
  tree->setHeaderLabels(QStringList() << tr("Check") << tr("Result"));
  tree->setRootIsDecorated(true);
  tree->setColumnWidth(0, 320);

  detailsHost = new QWidget;
  detailsLay = new QVBoxLayout(detailsHost);
  detailsLay->setAlignment(Qt::AlignTop);
  QScrollArea* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setWidget(detailsHost);

  QSplitter* split = new QSplitter;
  split->addWidget(tree);
  split->addWidget(scroll);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 1);

  // Optional Hardware Sniffer report: lets the CPU checks compare macOS with the real hardware.
  editReport = new QLineEdit;
  editReport->setPlaceholderText(tr("Optional: Report.json from Hardware Sniffer (for the CPU comparison checks)"));
  btnBrowse = new QPushButton(tr("Browse..."));
  QHBoxLayout* repRow = new QHBoxLayout;
  repRow->addWidget(new QLabel(tr("Hardware report:")));
  repRow->addWidget(editReport, 1);
  repRow->addWidget(btnBrowse);

  QHBoxLayout* bar = new QHBoxLayout;
  bar->addWidget(btnRun);
  bar->addWidget(btnCopy);
  bar->addStretch(1);

  QVBoxLayout* lay = new QVBoxLayout(this);
  lay->addWidget(summary);
  lay->addLayout(repRow);
  lay->addLayout(bar);
  lay->addWidget(split, 1);

  const QList<Check> checks = loadChecks();
  QMap<QString, QTreeWidgetItem*> groups;
  for (int i = 0; i < checks.size(); ++i) {
    QTreeWidgetItem*& g = groups[checks[i].group];
    if (!g) {
      g = new QTreeWidgetItem(tree, QStringList() << checks[i].group);
      g->setExpanded(true);
    }
    QTreeWidgetItem* it = new QTreeWidgetItem(g, QStringList() << checks[i].title << "-");
    it->setData(0, Qt::UserRole, i);
  }
  for (const Check& c : checks) {
    Result r;
    r.check = c;
    results << r;
  }

  connect(btnRun, &QPushButton::clicked, this, &dlgPostCheck::runAll);
  connect(btnCopy, &QPushButton::clicked, this, &dlgPostCheck::copyReport);
  connect(btnBrowse, &QPushButton::clicked, this, &dlgPostCheck::browseReport);
  connect(tree, &QTreeWidget::itemSelectionChanged, this, &dlgPostCheck::showDetails);

  if (!supported()) {
    summary->setText(tr("This check inspects the running macOS system, so it only works on the "
                        "Hackintosh itself. Run OCAT on that Mac to use it."));
    btnRun->setEnabled(false);
  } else if (checks.isEmpty()) {
    summary->setText(tr("No checks are available (checks.json could not be loaded)."));
    btnRun->setEnabled(false);
  } else {
    summary->setText(tr("Reads your system settings and is based on the Dortania "
                        "post-install guide (a few checks are general macOS advice and say so). "
                        "Nothing is changed unless you click Run on a fix and confirm it. "
                        "Click \"Run checks\" (the sleep log check can take several seconds)."));
  }
}

void dlgPostCheck::setStatus(QTreeWidgetItem* it, const Result& r) {
  it->setText(1, statusText(r.status));
  QColor c = r.status == Pass ? QColor("#2e7d32")
             : r.status == Fail ? (r.check.severity == "warn" ? QColor("#d32f2f") : QColor("#ef6c00"))
                                : r.status == Skipped ? QColor("#9e9e9e") : QColor("#757575");
  it->setForeground(1, c);
}

void dlgPostCheck::runAll() {
  if (running) return;
  running = true;
  btnRun->setEnabled(false);
  btnCopy->setEnabled(false);
  QApplication::setOverrideCursor(Qt::WaitCursor);

  // Read the report once; a bad file only disables the checks that need it.
  ctx = Context();
  reportNote.clear();
  fixMsg.clear();
  const QString repPath = editReport->text().trimmed();
  if (!repPath.isEmpty()) {
    QJsonObject rep;
    QStringList errs;
    if (EfiBuilder::loadReport(repPath, &rep, &errs)) {
      const QJsonObject cpu = rep["CPU"].toObject();
      ctx.cpuName = cpu["Processor Name"].toString();
      const QJsonValue cc = cpu["Core Count"];
      ctx.cores = cc.isString() ? cc.toString().trimmed() : (cc.isDouble() ? QString::number(cc.toInt()) : QString());
      for (QChar ch : ctx.cores)
        if (!ch.isDigit()) { ctx.cores.clear(); break; }
      if (ctx.cpuName.isEmpty()) reportNote = tr("The report has no CPU name.");
    } else {
      reportNote = tr("Could not read the report: %1").arg(errs.isEmpty() ? tr("unknown error") : errs.first());
    }
  }

  int n = 0, total = results.size();
  for (int g = 0; g < tree->topLevelItemCount(); ++g) {
    QTreeWidgetItem* grp = tree->topLevelItem(g);
    for (int c = 0; c < grp->childCount(); ++c) {
      QTreeWidgetItem* it = grp->child(c);
      const int idx = it->data(0, Qt::UserRole).toInt();
      summary->setText(tr("Running %1 of %2: %3").arg(++n).arg(total).arg(results[idx].check.title));
      it->setText(1, tr("running..."));
      QApplication::processEvents();
      if (!isVisible()) break;  // dialog was closed while running
      results[idx] = runCheck(results[idx].check, ctx);
      setStatus(it, results[idx]);
    }
  }
  QApplication::restoreOverrideCursor();

  refreshSummary();
  running = false;
  btnRun->setText(tr("Re-run checks"));
  btnRun->setEnabled(true);
  btnCopy->setEnabled(true);
  showDetails();
}

dlgPostCheck::~dlgPostCheck() {
  // Closing the window mid-fix must not let finished() reach a half-destroyed dialog.
  if (fixProc) {
    fixProc->disconnect(this);
    fixProc->kill();
    fixProc->waitForFinished(2000);
  }
}

void dlgPostCheck::refreshSummary() {
  int fail = 0, unk = 0, skipped = 0;
  for (const Result& r : results) {
    if (r.status == Fail) ++fail;
    if (r.status == Unknown) ++unk;
    if (r.status == Skipped) ++skipped;
  }
  summary->setText((fail == 0 ? tr("No problems found (%1 check(s) gave no verdict).").arg(unk)
                              : tr("%1 problem(s) found, %2 check(s) gave no verdict. Select a problem to see the fix.")
                                    .arg(fail).arg(unk)) +
                   (skipped ? "  " + tr("%1 skipped (not applicable to this Mac).").arg(skipped) : QString()) +
                   (reportNote.isEmpty() ? QString() : "  " + reportNote));
}

QTreeWidgetItem* dlgPostCheck::itemFor(int idx) const {
  for (int g = 0; g < tree->topLevelItemCount(); ++g)
    for (int c = 0; c < tree->topLevelItem(g)->childCount(); ++c)
      if (tree->topLevelItem(g)->child(c)->data(0, Qt::UserRole).toInt() == idx) return tree->topLevelItem(g)->child(c);
  return nullptr;
}

void dlgPostCheck::runFix(int idx, int fixIdx) {
  if (fixBusy || idx < 0 || idx >= results.size()) return;
  const Fix f = results[idx].check.fixes.value(fixIdx);
  if (!supported() || !canRunFix(f)) return;
  const bool admin = f.run == "admin";

  // Show what the setting is now, so it can be put back by hand.
  QString current;
  const QRegularExpressionMatch m = QRegularExpression("pmset -a ([a-z]+) ").match(f.cmd);
  if (admin && m.hasMatch()) {
    QProcess q;
    q.start("/bin/sh", QStringList() << "-c" << "pmset -g custom | grep -iw " + m.captured(1));
    if (q.waitForFinished(5000)) current = QString::fromUtf8(q.readAllStandardOutput()).trimmed();
  }
  QString text = tr("This will run:") + "\n\n" + f.cmd + "\n\n" + f.why;
  if (admin) text += "\n\n" + tr("macOS will ask for your administrator password.");
  if (admin && m.hasMatch())
    text += "\n\n" + (current.isEmpty() ? tr("pmset does not list this setting on this Mac right now.")
                                         : tr("Current value(s):") + "\n" + current);
  QMessageBox box(QMessageBox::Question, tr("Run this fix?"), text, QMessageBox::Yes | QMessageBox::No, this);
  box.setDefaultButton(QMessageBox::No);
  if (box.exec() != QMessageBox::Yes) return;

  QProcess* p = new QProcess(this);
  fixProc = p;
  fixBusy = true;
  summary->setText(tr("Running the fix..."));
  connect(p, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
          [this, p, idx, admin](int code, QProcess::ExitStatus st) { finishFix(p, idx, admin, code, st == QProcess::NormalExit); });
  connect(p, &QProcess::errorOccurred, this, [this, p, idx, admin](QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart) finishFix(p, idx, admin, -1, false);
  });
  if (admin) p->start("/usr/bin/osascript", QStringList() << "-e" << adminScript(f.cmd));
  else p->start("/bin/sh", QStringList() << "-c" << f.cmd);
  // The password prompt can stay open a while; a hung command must not hang the dialog.
  QTimer::singleShot(admin ? 300000 : 60000, p, [p] {
    if (p->state() != QProcess::NotRunning) p->kill();
  });
}

void dlgPostCheck::finishFix(QProcess* p, int idx, bool admin, int code, bool normalExit) {
  if (p->property("done").toBool()) return;
  p->setProperty("done", true);
  const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
  const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
  p->deleteLater();
  fixBusy = false;

  QString msg;
  if (admin && code != 0 && err.contains("-128")) {  // the user dismissed macOS's password prompt
    msg = tr("Cancelled. Nothing was changed.");
  } else if (normalExit && code == 0) {
    msg = tr("Ran successfully.");
    if (!out.isEmpty()) msg += "\n" + out.left(1500);
    if (admin && idx >= 0 && idx < results.size()) {
      results[idx] = runCheck(results[idx].check, ctx);  // does the check pass now?
      if (QTreeWidgetItem* it = itemFor(idx)) setStatus(it, results[idx]);
      msg += "\n" + (results[idx].status == Pass ? tr("The check now passes.")
                                                  : tr("The check still reports: %1").arg(statusText(results[idx].status)));
    }
  } else {
    msg = tr("The command failed (exit code %1).").arg(code);
    const QString detail = err.isEmpty() ? out : err;
    if (!detail.isEmpty()) msg += "\n" + detail.left(1500);
  }
  fixMsg[idx] = msg;
  refreshSummary();
  showDetails();
}

void dlgPostCheck::clearDetails() {
  while (QLayoutItem* it = detailsLay->takeAt(0)) {
    if (it->widget()) it->widget()->deleteLater();
    delete it;
  }
}

void dlgPostCheck::showDetails() {
  clearDetails();
  const QList<QTreeWidgetItem*> sel = tree->selectedItems();
  if (sel.isEmpty() || sel.first()->parent() == nullptr) return;
  const int idx = sel.first()->data(0, Qt::UserRole).toInt();
  const Result& r = results[idx];

  QLabel* head = new QLabel("<b>" + r.check.title.toHtmlEscaped() + "</b> - " + statusText(r.status));
  head->setWordWrap(true);
  detailsLay->addWidget(head);
  if (fixMsg.contains(idx)) {
    QLabel* fm = new QLabel("<b>" + tr("Last fix:") + "</b><pre>" + fixMsg.value(idx).toHtmlEscaped() + "</pre>");
    fm->setWordWrap(true);
    fm->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailsLay->addWidget(fm);
  }

  if (r.status == Unknown && r.reason.isEmpty()) {
    detailsLay->addWidget(new QLabel(tr("Not run yet.")));
  } else {
    QLabel* why = new QLabel(r.status == Fail ? r.check.why : r.reason);
    why->setWordWrap(true);
    detailsLay->addWidget(why);
  }

  if (r.status == Fail) {
    for (int i = 0; i < r.check.fixes.size(); ++i) {
      const Fix& f = r.check.fixes[i];
      if (f.cmd.isEmpty()) {
        QLabel* n = new QLabel(tr("Step: ") + f.note);
        n->setWordWrap(true);
        detailsLay->addWidget(n);
        continue;
      }
      QLabel* w = new QLabel(f.why);
      w->setWordWrap(true);
      QLabel* cmd = new QLabel("<code>" + f.cmd.toHtmlEscaped() + "</code>");
      cmd->setTextInteractionFlags(Qt::TextSelectableByMouse);
      cmd->setWordWrap(true);
      QPushButton* copy = new QPushButton(tr("Copy"));
      QPushButton* term = new QPushButton(tr("Run in Terminal"));
      term->setToolTip(tr("Opens Terminal with the command shown. It runs only after you press "
                          "Return there, and you enter any password yourself."));
      term->setEnabled(supported());
      const QString command = f.cmd;
      connect(copy, &QPushButton::clicked, this, [command] { QApplication::clipboard()->setText(command); });
      connect(term, &QPushButton::clicked, this, [this, command] {
        QString err;
        if (!openInTerminal(command, &err))
          QMessageBox::warning(this, tr("Terminal"), tr("Could not open Terminal: %1").arg(err));
      });
      QHBoxLayout* row = new QHBoxLayout;
      row->addWidget(copy);
      row->addWidget(term);
      if (!f.run.isEmpty()) {
        QPushButton* runBtn = new QPushButton(f.run == "admin" ? tr("Run (asks for password)") : tr("Run here"));
        runBtn->setToolTip(tr("Asks you to confirm the exact command first."));
        runBtn->setEnabled(supported() && canRunFix(f) && !fixBusy);
        const int fi = i;
        connect(runBtn, &QPushButton::clicked, this, [this, idx, fi] { runFix(idx, fi); });
        row->addWidget(runBtn);
      }
      row->addStretch(1);
      QWidget* rw = new QWidget;
      rw->setLayout(row);
      detailsLay->addWidget(w);
      detailsLay->addWidget(cmd);
      detailsLay->addWidget(rw);
    }
  }
  if (!r.output.isEmpty() && r.status != Pass) {
    QLabel* o = new QLabel("<small>" + tr("Command output:") + "<pre>" +
                           r.output.left(1200).toHtmlEscaped() + "</pre></small>");
    o->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailsLay->addWidget(o);
  }
  if (QUrl(r.check.guide).scheme() == "https") {
    QLabel* g = new QLabel("<a href=\"" + r.check.guide.toHtmlEscaped() + "\">" + tr("Open the guide page") + "</a>");
    g->setOpenExternalLinks(true);
    detailsLay->addWidget(g);
  }
}

void dlgPostCheck::browseReport() {
  const QString f = QFileDialog::getOpenFileName(this, tr("Hardware Sniffer report"),
                                                 QFileInfo(editReport->text()).absolutePath(),
                                                 tr("Report (*.json);;All files (*)"));
  if (!f.isEmpty()) editReport->setText(f);
}

void dlgPostCheck::copyReport() {
  QApplication::clipboard()->setText(reportText(results));
  summary->setText(summary->text() + "  " + tr("(report copied)"));
}
