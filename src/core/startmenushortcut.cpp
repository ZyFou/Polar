#include "startmenushortcut.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#ifdef POLAR_TESTING
namespace { QString testDirectory; }
void StartMenuShortcut::setDirectoryForTests(const QString &directory) {testDirectory=directory;}
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <shobjidl.h>
#include <objbase.h>
namespace {
struct Link {
    HRESULT init = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IShellLinkW *shell = nullptr;
    IPersistFile *file = nullptr;
    Link() {
        if(FAILED(init) && init!=RPC_E_CHANGED_MODE) return;
        if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,
            IID_IShellLinkW,reinterpret_cast<void **>(&shell))))
            shell->QueryInterface(IID_IPersistFile,reinterpret_cast<void **>(&file));
    }
    ~Link() {if(file) file->Release();if(shell) shell->Release();if(SUCCEEDED(init)) CoUninitialize();}
};
QString directory() {
#ifdef POLAR_TESTING
    if(!testDirectory.isEmpty()) return testDirectory;
#endif
    return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
}
QString path() {return QDir(directory()).filePath("Polar.lnk");}
}
#endif
StartMenuShortcut::State StartMenuShortcut::inspect(const QString &target) {
#ifdef Q_OS_WIN
    // QFileInfo follows Windows shortcuts; a dangling .lnk must remain repairable.
    const auto nativeLink=QDir::toNativeSeparators(path());
    if(GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()))==INVALID_FILE_ATTRIBUTES) {
        const auto error=GetLastError();
        return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND?State::Missing:State::Invalid;
    }
    Link link;
    if(!link.file || FAILED(link.file->Load(reinterpret_cast<LPCWSTR>(nativeLink.utf16()),STGM_READ))) return State::Invalid;
    wchar_t resolved[32768] = {};
    if(FAILED(link.shell->GetPath(resolved,32768,nullptr,SLGP_RAWPATH))) return State::Invalid;
    const QFileInfo actual(QString::fromWCharArray(resolved)),expected(target);
    // Full canonical path comparison checks both directory and executable filename.
    return actual.exists() && expected.exists()
        && actual.canonicalFilePath().compare(expected.canonicalFilePath(),Qt::CaseInsensitive)==0
        ? State::Valid : State::Invalid;
#else
    Q_UNUSED(target);return State::Unsupported;
#endif
}
bool StartMenuShortcut::create(const QString &target) {
#ifdef Q_OS_WIN
    const auto startDirectory=directory();
    if(startDirectory.isEmpty() || !QFileInfo(target).isFile() || !QDir().mkpath(startDirectory)) return false;
    Link link;
    if(!link.file) return false;
    const auto native=QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath());
    const auto work=QDir::toNativeSeparators(QFileInfo(target).absolutePath());
    if(FAILED(link.shell->SetPath(reinterpret_cast<LPCWSTR>(native.utf16())))
        || FAILED(link.shell->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(work.utf16())))
        || FAILED(link.shell->SetDescription(L"Polar"))
        || FAILED(link.shell->SetIconLocation(reinterpret_cast<LPCWSTR>(native.utf16()),0))) return false;
    const auto nativeLink=QDir::toNativeSeparators(path());
    return SUCCEEDED(link.file->Save(reinterpret_cast<LPCWSTR>(nativeLink.utf16()),TRUE));
#else
    Q_UNUSED(target);return false;
#endif
}
