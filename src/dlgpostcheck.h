#ifndef DLGPOSTCHECK_H
#define DLGPOSTCHECK_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "postcheck/postcheck.h"

// Tools > Post-Install Check: runs read-only checks on this Hackintosh and, for
// failures, shows the Dortania fix commands. Commands are only copied or opened
// (not executed) in Terminal, where the user confirms and enters any password.
class dlgPostCheck : public QDialog {
  Q_OBJECT
 public:
  explicit dlgPostCheck(QWidget* parent = nullptr);

 private slots:
  void runAll();
  void showDetails();
  void copyReport();
  void browseReport();

 private:
  void setStatus(QTreeWidgetItem* it, const PostCheck::Result& r);
  void clearDetails();

  QTreeWidget* tree;
  QWidget* detailsHost;
  QVBoxLayout* detailsLay;
  QLabel* summary;
  QPushButton *btnRun, *btnCopy, *btnBrowse;
  QLineEdit* editReport;
  QList<PostCheck::Result> results;
  bool running = false;
};

#endif
