#ifndef POSTCHECK_H
#define POSTCHECK_H

#include <QList>
#include <QString>
#include <QStringList>

// Post-install health check for a running Hackintosh (macOS only), based on the
// Dortania OpenCore Post-Install guide. Checks are data (checks.json); every
// check command is read-only and never needs sudo. A fix is only run by the app
// when its data opts in ("run") AND canRunFix() accepts the exact command, after
// the user confirms it; everything else is only shown, copied or opened in Terminal.
namespace PostCheck {

// Skipped = the check does not apply to this Mac (e.g. no laptop keyboard/trackpad).
enum Status { Pass, Fail, Unknown, Skipped };

struct Fix {
  QString cmd;   // shell command to show/copy (may need sudo)
  QString why;   // what it does
  QString note;  // free text step when there is no single command
  QString run;   // "" = show only, "user" = read-only command, "admin" = pmset setting
};

struct Check {
  QString id, group, title, cmd, passRx, failRx, skipRx, skipWhy, severity, guide, why;
  QList<Fix> fixes;
  int timeoutSec = 20;
  bool needsReport = false;  // uses OCAT_REPORT_* from a Hardware Sniffer report
};

// Values from a Hardware Sniffer report, passed to checks as OCAT_REPORT_CPU / OCAT_REPORT_CORES.
struct Context {
  QString cpuName;
  QString cores;  // digits only, or empty
};

struct Result {
  Check check;
  Status status = Unknown;
  QString reason;  // why the status was chosen (shown in details)
  QString output;  // trimmed command output
};

// Only macOS can run the checks.
bool supported();
QList<Check> loadChecks();

// Pure function so it can be tested with canned output. `ran` is false when the
// command could not be started or timed out. A failing match wins over a
// passing one (e.g. one of two power profiles is wrong); no match at all is
// Unknown, never a guess.
Status evaluate(const Check& c, const QString& output, bool ran, QString* reason);

Result runCheck(const Check& c, const Context& ctx = Context());
QString reportText(const QList<Result>& results);

// Opens Terminal.app showing the command and waiting for Return before it runs.
bool openInTerminal(const QString& cmd, QString* error);

// Whether the app may run this fix itself. "admin" is limited to
// `sudo pmset -a <setting> <number>`; "user" must be a single line without sudo.
// Anything else is refused, whatever the data says.
bool canRunFix(const Fix& f);

// AppleScript source that runs `cmd` as administrator (a leading "sudo " is dropped;
// macOS shows its own password prompt).
QString adminScript(const QString& cmd);

}  // namespace PostCheck

#endif
