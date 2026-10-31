#ifndef SYSINFO_H
#define SYSINFO_H

#include <QThread>
#include <QVariantList>

// Collects read-only facts about this computer (like Hackintool's System tab).
// Runs the slow probes (system_profiler, wmic...) off the GUI thread; each row of
// the result is a QStringList {section, label, value}.
class SysInfoCollector : public QThread {
  Q_OBJECT
 public:
  using QThread::QThread;
  static QVariantList collect();  // blocking; also used by tests

 signals:
  void collected(const QVariantList& rows);

 protected:
  void run() override { emit collected(collect()); }
};

#endif  // SYSINFO_H
