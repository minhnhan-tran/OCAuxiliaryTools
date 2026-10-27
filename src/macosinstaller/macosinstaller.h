#ifndef MACOSINSTALLER_H
#define MACOSINSTALLER_H

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

// Create a bootable macOS installer USB (macOS only). The flow follows the one in
// Dortania's OpenCore Legacy Patcher: pick or download an "Install macOS *.app",
// erase an external disk, run Apple's createinstallmedia, optionally copy an EFI
// folder onto the stick. Everything destructive is only ever BUILT here as one
// command line; the dialog hands it to Terminal so the user reviews it and types
// the sudo password there. The helpers below are pure so they can be tested.
namespace MacInstaller {

struct Installer {      // an "Install macOS *.app" found on disk
  QString path;         // /Applications/Install macOS Tahoe.app
  QString name;         // "Install macOS Tahoe"
  QString version;      // CFBundleShortVersionString, "" if unreadable
};

struct Product {        // a full installer in Apple's software-update catalog (InstallAssistant.pkg)
  QString id, title, version, build;
  QUrl pkgUrl, distUrl;
  qint64 size = 0;
  QDateTime date;
};

struct Disk {           // an external physical whole disk that may be erased
  QString id;           // "disk4"
  QString name;         // media name, e.g. "SanDisk Ultra"
  QString bus;          // "USB"
  qint64 size = 0;      // bytes
};

constexpr qint64 kMinUsbBytes = 15LL * 1000 * 1000 * 1000;  // createinstallmedia needs ~16 GB
constexpr qint64 kRecommendedUsbBytes = 30LL * 1000 * 1000 * 1000;

// Apple's public catalog of everything Software Update can offer (the one OCLP reads).
QUrl catalogUrl();
bool trustedAppleUrl(const QUrl& u);   // https and a host under apple.com
// Products of the parsed catalog plist that carry an InstallAssistant.pkg (title/version/build
// still empty: they come from the product's distribution file). Untrusted URLs are dropped.
QList<Product> parseCatalog(const QVariantMap& root);
bool parseDistribution(const QString& xml, Product* p);  // fills title, version, build
QString pkgFileName(const Product& p);                   // "" if version/build are not path-safe
bool packageSigned(const QString& pkgutilCheckSignature);  // output of `pkgutil --check-signature`
QString installPkgCommand(const QString& pkg);           // sudo installer -pkg ... -target /
QString humanSize(qint64 bytes);
bool isBetaBuild(const QString& build);  // "25A5316i": a trailing lowercase letter marks a seed build
bool newerFirst(const Product& a, const Product& b);
bool validVersion(const QString& v);   // "26.7.2" - safe to put into a command
bool validDiskId(const QString& id);   // "disk4"  - whole disks only

// Whole-disk ids that hold the running system, from `diskutil info -plist /` as JSON
// (the APFS container's physical stores, plus its parent disk).
QStringList bootDiskIds(const QJsonObject& rootInfo);

// `diskutil info -plist diskN` as JSON -> a Disk, or false (with *why) if it must not be erased.
bool eligibleDisk(const QJsonObject& info, const QStringList& bootDisks, Disk* out, QString* why);

// "" if `dir` looks like an OpenCore EFI folder (BOOT/BOOTx64.efi and OC/config.plist), else why not.
QString checkEfiFolder(const QString& dir);

// "" if installerApp is not an Install macOS app with createinstallmedia or the disk id is bad.
// The volume is named after the disk so a leftover volume from another stick can never be targeted.
QString createCommand(const QString& installerApp, const Disk& disk, const QString& efiFolder);
QString volumeLabel(const Disk& disk);

#ifdef Q_OS_MAC
QList<Installer> findInstallers(const QString& dir = "/Applications");
QList<Disk> listExternalDisks(QStringList* skipped, QString* error);
#endif

QString shQuote(const QString& s);

}  // namespace MacInstaller

#endif
