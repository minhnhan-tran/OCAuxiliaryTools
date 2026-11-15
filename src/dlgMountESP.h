#ifndef DLGMOUNTESP_H
#define DLGMOUNTESP_H

#include <QDialog>

namespace Ui {
class dlgMountESP;
}

class dlgMountESP : public QDialog {
  Q_OBJECT

 public:
  explicit dlgMountESP(QWidget* parent = nullptr);
  ~dlgMountESP();
  Ui::dlgMountESP* ui;

  void mountESP(bool openConfig);
  // Marks every row as mounted (green dot, mount point in the tooltip) or not mounted
  // (grey dot, dimmed text). The row text itself is left alone: mountESP() parses it.
  void refreshStates();
 private slots:
  void on_btnMount_clicked();

  void on_btnMountOpenConfig_clicked();

  void on_btnOpenEfiFolder_clicked();

  void on_listWidget_currentRowChanged(int row);

 private:
  void updateButtons();
};

#endif  // DLGMOUNTESP_H
