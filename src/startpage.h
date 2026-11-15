#ifndef STARTPAGE_H
#define STARTPAGE_H

#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QWidget>

class SysInfoCollector;

// Not called "StartPage": <wingdi.h> declares a function StartPage(), which hides the class on Windows.
// Landing page shown when no config.plist is open: this computer's hardware and
// boot facts (like Hackintool's System tab) plus shortcuts to open a config.
class HomePage : public QWidget {
  Q_OBJECT
 public:
  explicit HomePage(QWidget* parent = nullptr);
  ~HomePage() override;
  void refresh();  // re-run the probes in a worker thread

 signals:
  void openRequested();
  void editorRequested();

 private:
  void fill(const QVariantList& rows);
  QString asText() const;

  QTreeWidget* tree_ = nullptr;
  QLabel* lblStatus_ = nullptr;
  QPushButton* btnRefresh_ = nullptr;
  SysInfoCollector* worker_ = nullptr;
};

#endif  // STARTPAGE_H
