#include "postcheck.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>

namespace PostCheck {

bool supported() {
#ifdef Q_OS_MAC
  return true;
#else
  return false;
#endif
}

QList<Check> loadChecks() {
  QFile f(":/postcheck/checks.json");
  if (!f.open(QIODevice::ReadOnly)) {
    f.setFileName(qEnvironmentVariable("POSTCHECK_PATH"));  // test harness
    if (!f.open(QIODevice::ReadOnly)) return {};
  }
  QList<Check> out;
  const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).object()["checks"].toArray();
  for (const QJsonValue& v : arr) {
    const QJsonObject o = v.toObject();
    Check c;
    c.id = o["id"].toString();
    c.group = o["group"].toString();
    c.title = o["title"].toString();
    c.cmd = o["cmd"].toString();
    c.passRx = o["pass"].toString();
    c.failRx = o["fail"].toString();
    c.severity = o["severity"].toString("info");
    c.guide = o["guide"].toString();
    c.why = o["why"].toString();
    c.timeoutSec = o["timeoutSec"].toInt(20);
    c.needsReport = o["needsReport"].toBool();
    for (const QJsonValue& fv : o["fixes"].toArray()) {
      const QJsonObject fo = fv.toObject();
      Fix fx;
      fx.cmd = fo["cmd"].toString();
      fx.why = fo["why"].toString();
      fx.note = fo["note"].toString();
      c.fixes << fx;
    }
    if (!c.id.isEmpty() && !c.cmd.isEmpty()) out << c;
  }
  return out;
}

Status evaluate(const Check& c, const QString& output, bool ran, QString* reason) {
  auto say = [&](const QString& s) { if (reason) *reason = s; };
  if (!ran) { say("The command could not be run or timed out."); return Unknown; }
  const QRegularExpression::PatternOption mo = QRegularExpression::MultilineOption;
  if (!c.failRx.isEmpty()) {
    QRegularExpression re(c.failRx, mo);
    if (!re.isValid()) { say("Invalid fail rule in checks.json."); return Unknown; }
    if (re.match(output).hasMatch()) { say("The output matches the failing pattern."); return Fail; }
  }
  if (!c.passRx.isEmpty()) {
    QRegularExpression re(c.passRx, mo);
    if (!re.isValid()) { say("Invalid pass rule in checks.json."); return Unknown; }
    if (re.match(output).hasMatch()) { say("The output matches the expected pattern."); return Pass; }
  }
  say("The output did not contain what this check looks for (the setting may not exist on this machine), so no verdict.");
  return Unknown;
}

Result runCheck(const Check& c, const Context& ctx) {
  Result r;
  r.check = c;
  if (c.needsReport && ctx.cpuName.trimmed().isEmpty()) {
    r.status = Unknown;
    r.reason = "Choose a Hardware Sniffer report (Report.json) at the top of this window to run this check.";
    return r;
  }
  QProcess p;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert("OCAT_REPORT_CPU", ctx.cpuName);
  env.insert("OCAT_REPORT_CORES", ctx.cores);
  p.setProcessEnvironment(env);
  p.setProcessChannelMode(QProcess::MergedChannels);
  p.start("/bin/sh", QStringList() << "-c" << c.cmd);
  bool ran = p.waitForStarted(5000);
  if (ran) {
    ran = p.waitForFinished(c.timeoutSec * 1000);
    if (!ran) { p.kill(); p.waitForFinished(2000); }
  }
  QString out = ran ? QString::fromUtf8(p.readAll()) : QString();
  // csrutil, nvram etc. exit non-zero while still printing a usable answer, so
  // the exit code alone is not an error; only a missing/timed-out process is.
  if (out.size() > 4000) out = out.left(4000) + "\n...";
  r.output = out.trimmed();
  r.status = evaluate(c, out, ran, &r.reason);
  return r;
}

QString reportText(const QList<Result>& rs) {
  static const char* names[] = {"PASS", "FAIL", "UNKNOWN"};
  QString t = QString("OCAT post-install check (%1, macOS %2)\n\n")
                  .arg(QSysInfo::machineHostName().isEmpty() ? "mac" : "this Mac", QSysInfo::productVersion());
  for (const Result& r : rs)
    t += QString("[%1] %2 / %3\n").arg(names[r.status], r.check.group, r.check.title);
  for (const Result& r : rs) {
    if (r.status != Fail) continue;
    t += "\n--- " + r.check.title + " ---\n" + r.check.why + "\n";
    for (const Fix& f : r.check.fixes) {
      if (!f.cmd.isEmpty()) t += "  $ " + f.cmd + "   # " + f.why + "\n";
      else if (!f.note.isEmpty()) t += "  - " + f.note + "\n";
    }
  }
  return t;
}

namespace {
QString shQuote(const QString& s) { QString q = s; q.replace("'", "'\\''"); return "'" + q + "'"; }
QString asQuote(const QString& s) { QString q = s; q.replace("\\", "\\\\").replace("\"", "\\\""); return "\"" + q + "\""; }
}  // namespace

bool openInTerminal(const QString& cmd, QString* error) {
#ifdef Q_OS_MAC
  if (cmd.trimmed().isEmpty() || cmd.contains('\n') || cmd.contains('\r')) {
    if (error) *error = "Refusing to open an empty or multi-line command.";
    return false;
  }
  // do script runs immediately, so the line first prints the command and
  // waits for Return: the user reviews it and types any sudo password in Terminal.
  // The braces keep a ";" inside cmd from escaping the "wait for Return" guard.
  const QString line = "printf '%s\\n\\n' " + shQuote("Command: " + cmd) +
                       "; printf 'Press Return to run it, Ctrl+C to cancel. '; read -r ocat_go && { " + cmd + "; }";
  const QStringList args = QStringList()
      << "-e" << "tell application \"Terminal\" to activate"
      << "-e" << "tell application \"Terminal\" to do script " + asQuote(line);
  QProcess p;
  p.start("osascript", args);
  if (!p.waitForFinished(15000) || p.exitCode() != 0) {
    if (error) *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return false;
  }
  return true;
#else
  Q_UNUSED(cmd);
  if (error) *error = "Terminal is only available on macOS.";
  return false;
#endif
}

}  // namespace PostCheck
