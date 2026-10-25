#ifndef DLGPOSTCHECK_H
#define DLGPOSTCHECK_H

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QProcess>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "postcheck/postcheck.h"

// Tools > Post-Install Check: runs read-only checks on this Hackintosh and, for
// failures, shows the Dortania fix commands. A fix can be copied, opened in Terminal
// (runs only after Return there), or - for the few fixes that opt in - run here after a
// confirmation; admin fixes use macOS's own password prompt. The check is then re-run.
class dlgPostCheck : public QDialog {
  Q_OBJECT
 public:
  explicit dlgPostCheck(QWidget* parent = nullptr);
  ~dlgPostCheck() override;

 private slots:
  void runAll();
  void showDetails();
  void copyReport();
  void browseReport();

 private:
  void setStatus(QTreeWidgetItem* it, const PostCheck::Result& r);
  void clearDetails();
  void runFix(int resultIdx, int fixIdx);
  void finishFix(QProcess* p, int idx, bool admin, int code, bool normalExit);
  void refreshSummary();
  QTreeWidgetItem* itemFor(int idx) const;

  QTreeWidget* tree;
  QWidget* detailsHost;
  QVBoxLayout* detailsLay;
  QLabel* summary;
  QPushButton *btnRun, *btnCopy, *btnBrowse;
  QLineEdit* editReport;
  QList<PostCheck::Result> results;
  bool running = false;
  PostCheck::Context ctx;  // report values used by the last run
  QString reportNote;
  QHash<int, QString> fixMsg;  // result index -> outcome of a fix run here
  bool fixBusy = false;
  QPointer<QProcess> fixProc;  // fix still running, stopped if the window closes
};

#endif
