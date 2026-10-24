// Kext downloader for the EFI Builder. Source order follows OpCore-Simplify
// (BSD-3-Clause): Dortania build-repo, then a direct URL, then GitHub releases.
#pragma once
#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>

namespace EfiBuilder {

struct FetchOptions {
  QString proxyHost;      // empty = no proxy
  int proxyPort = 0;
  QString token;          // optional GitHub API token
  QString unzipProgram = "unzip";
};

struct FetchResult {
  QStringList ok;
  QMap<QString, QString> failed;  // kext name -> reason
};

// Return false from the callback to cancel.
typedef std::function<bool(const QString& status)> FetchProgress;

// Downloads each kext and installs "<name>.kext" into destDir.
FetchResult fetchKexts(const QStringList& names, int darwinMajor, const QString& destDir,
                       const FetchOptions& opt, const FetchProgress& progress);

}  // namespace EfiBuilder
