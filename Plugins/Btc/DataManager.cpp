#include "pch.h"
#include "DataManager.h"

CDataManager CDataManager::m_instance;

extern "C" IMAGE_DOS_HEADER __ImageBase;

CDataManager::CDataManager()
{
    //初始化DPI
    HDC hDC = ::GetDC(HWND_DESKTOP);
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ::ReleaseDC(HWND_DESKTOP, hDC);
}

CDataManager::~CDataManager()
{
    SaveConfig();
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

static void WritePrivateProfileInt(const wchar_t* app_name, const wchar_t* key_name, int value, const wchar_t* file_path)
{
    wchar_t buff[16];
    swprintf_s(buff, L"%d", value);
    WritePrivateProfileString(app_name, key_name, buff, file_path);
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    // 配置文件：默认使用 dll 同名 ini，例如 Btc.dll -> Btc.ini
    HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(hModule, module_path, MAX_PATH);
    std::wstring module_path_str = module_path;
    size_t pos = module_path_str.find_last_of(L"\\/");
    std::wstring module_dir;
    if (pos != std::wstring::npos)
        module_dir = module_path_str.substr(0, pos + 1);

    std::wstring module_file_name = (pos != std::wstring::npos) ? module_path_str.substr(pos + 1) : module_path_str;
    pos = module_file_name.find_last_of(L'.');
    if (pos != std::wstring::npos)
        module_file_name = module_file_name.substr(0, pos);

    const std::wstring& base_dir = config_dir.empty() ? module_dir : config_dir;
    m_config_path = base_dir + module_file_name + L".ini";
}

void CDataManager::SaveConfig() const
{
    // Phase 1+：按需持久化配置项
}

const CString& CDataManager::StringRes(UINT id)
{
    auto iter = m_string_table.find(id);
    if (iter != m_string_table.end())
    {
        return iter->second;
    }
    else
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        m_string_table[id].LoadString(id);
        return m_string_table[id];
    }
}

void CDataManager::DPIFromWindow(CWnd* pWnd)
{
    CWindowDC dc(pWnd);
    HDC hDC = dc.GetSafeHdc();
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
}

int CDataManager::DPI(int pixel)
{
    return m_dpi * pixel / 96;
}

float CDataManager::DPIF(float pixel)
{
    return m_dpi * pixel / 96;
}

int CDataManager::RDPI(int pixel)
{
    return pixel * 96 / m_dpi;
}

HICON CDataManager::GetIcon(UINT id)
{
    auto iter = m_icons.find(id);
    if (iter != m_icons.end())
    {
        return iter->second;
    }
    else
    {
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        HICON hIcon = (HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(id), IMAGE_ICON, DPI(16), DPI(16), 0);
        m_icons[id] = hIcon;
        return hIcon;
    }
}
