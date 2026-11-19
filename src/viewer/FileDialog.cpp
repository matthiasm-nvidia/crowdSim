#define NOMINMAX
#include <shobjidl.h>
#include <windows.h>
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include "FileDialog.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <filesystem>
#include <stdexcept>

std::string selectUsdFile(GLFWwindow *owner)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        throw std::runtime_error("Could not initialize the file dialog.");
    IFileOpenDialog *dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    std::string selected;
    if (SUCCEEDED(result))
    {
        const COMDLG_FILTERSPEC filters[] = {{L"USD stages", L"*.usd;*.usda;*.usdc;*.usdz"}, {L"All files", L"*.*"}};
        dialog->SetFileTypes(2, filters);
        dialog->SetTitle(L"Load USD stage");
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        result = dialog->Show(glfwGetWin32Window(owner));
        if (SUCCEEDED(result))
        {
            IShellItem *item = nullptr;
            result = dialog->GetResult(&item);
            if (SUCCEEDED(result))
            {
                PWSTR path = nullptr;
                result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(result))
                {
                    selected = std::filesystem::path(path).u8string();
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        throw std::runtime_error("The file dialog failed.");
    return selected;
}
