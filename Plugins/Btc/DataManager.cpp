#include "pch.h"
#include "DataManager.h"
#include "../utilities/IniHelper.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdarg>

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
    m_log_path = base_dir + module_file_name + L".log";

    utilities::CIniHelper ini(m_config_path);
    ini.GetStringList(L"config", L"symbols", m_setting_data.symbols, std::vector<std::wstring>{});
    m_setting_data.active_symbol = ini.GetString(L"config", L"active_symbol", L"");
    m_setting_data.update_interval_sec = ini.GetInt(L"config", L"update_interval_sec", 5);
    m_setting_data.tooltip_max_coins = ini.GetInt(L"config", L"tooltip_max_coins", 8);
    m_setting_data.stale_threshold_sec = ini.GetInt(L"config", L"stale_threshold_sec", 30);
    m_setting_data.second_line_dual_symbol = ini.GetBool(L"config", L"second_line_dual_symbol", true);
    m_setting_data.debug_log_enabled = ini.GetBool(L"debug", L"log_enabled", false);
    m_setting_data.debug_log_level = ini.GetInt(L"debug", L"log_level", 1);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        EnsureDefaultsLocked();
    }

    // Phase 1：没有网络时也可验收 UI 行为
    UpdateMockQuotes();
}

void CDataManager::SaveConfig() const
{
    if (m_config_path.empty())
        return;

    utilities::CIniHelper ini(m_config_path);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ini.WriteStringList(L"config", L"symbols", m_setting_data.symbols);
        ini.WriteString(L"config", L"active_symbol", m_setting_data.active_symbol);
        ini.WriteInt(L"config", L"update_interval_sec", m_setting_data.update_interval_sec);
        ini.WriteInt(L"config", L"tooltip_max_coins", m_setting_data.tooltip_max_coins);
        ini.WriteInt(L"config", L"stale_threshold_sec", m_setting_data.stale_threshold_sec);
        ini.WriteBool(L"config", L"second_line_dual_symbol", m_setting_data.second_line_dual_symbol);
        ini.WriteBool(L"debug", L"log_enabled", m_setting_data.debug_log_enabled);
        ini.WriteInt(L"debug", L"log_level", m_setting_data.debug_log_level);
    }
    ini.Save();
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

void CDataManager::EnsureDefaultsLocked()
{
    if (m_setting_data.symbols.empty())
        m_setting_data.symbols = { L"BTCUSDT", L"ETHUSDT", L"SOLUSDT" };

    if (m_setting_data.update_interval_sec < 1)
        m_setting_data.update_interval_sec = 1;
    if (m_setting_data.tooltip_max_coins < 1)
        m_setting_data.tooltip_max_coins = 1;
    if (m_setting_data.stale_threshold_sec < 1)
        m_setting_data.stale_threshold_sec = 1;

    if (m_setting_data.active_symbol.empty())
        m_setting_data.active_symbol = m_setting_data.symbols.front();

    // active_symbol 不在列表时，回落到第一个
    bool found{};
    for (const auto& s : m_setting_data.symbols)
    {
        if (_wcsicmp(s.c_str(), m_setting_data.active_symbol.c_str()) == 0)
        {
            found = true;
            m_setting_data.active_symbol = s;
            break;
        }
    }
    if (!found && !m_setting_data.symbols.empty())
        m_setting_data.active_symbol = m_setting_data.symbols.front();
}

std::wstring CDataManager::FormatPrice(double price) const
{
    // Phase 1：简单策略；Phase 3+ 再引入精度规则与单位缩写
    if (price <= 0.0)
        return L"--";

    int decimals = 2;
    if (price < 1.0)
        decimals = 6;
    else if (price < 100.0)
        decimals = 4;

    std::wstringstream wss;
    wss << std::fixed << std::setprecision(decimals) << price;
    return wss.str();
}

std::wstring CDataManager::FormatSignedPct(double pct) const
{
    std::wstringstream wss;
    wss << std::fixed << std::setprecision(2);
    if (pct > 0.0)
        wss << L'+' << pct << L'%';
    else
        wss << pct << L'%';
    return wss.str();
}

std::wstring CDataManager::FormatCoreLineLocked(const Quote& quote) const
{
    std::wstring symbol = quote.symbol.empty() ? m_setting_data.active_symbol : quote.symbol;
    if (!quote.is_ok)
    {
        std::wstring line = symbol;
        line += L" --";
        if (!quote.error.empty())
        {
            line += L" (";
            line += quote.error;
            line += L')';
        }
        return line;
    }

    std::wstring line = symbol;
    line += L' ';
    line += FormatPrice(quote.last);
    line += L' ';
    line += FormatSignedPct(quote.change_pct);
    return line;
}

void CDataManager::RebuildRenderCacheLocked()
{
    EnsureDefaultsLocked();

    const auto& symbols = m_setting_data.symbols;
    const std::wstring& active = m_setting_data.active_symbol;

    Quote active_quote{};
    active_quote.symbol = active;
    auto it = m_quotes.find(active);
    if (it != m_quotes.end())
        active_quote = it->second;

    m_render_cache.line1 = FormatCoreLineLocked(active_quote);

    // 第二行：默认显示下一个币种的核心行，形成多币种扫视
    Quote line2_quote{};
    if (!symbols.empty())
    {
        size_t active_index{};
        for (size_t i = 0; i < symbols.size(); ++i)
        {
            if (_wcsicmp(symbols[i].c_str(), active.c_str()) == 0)
            {
                active_index = i;
                break;
            }
        }
        size_t next_index = (active_index + 1) % symbols.size();
        line2_quote.symbol = symbols[next_index];
        auto it2 = m_quotes.find(line2_quote.symbol);
        if (it2 != m_quotes.end())
            line2_quote = it2->second;
    }
    m_render_cache.line2 = FormatCoreLineLocked(line2_quote);

    // Tooltip：多币种概览 + 状态行（Phase 4 完善）
    std::wstringstream tip;
    int shown = 0;
    int max_show = m_setting_data.tooltip_max_coins;
    for (const auto& s : symbols)
    {
        if (shown >= max_show)
            break;
        Quote q{};
        q.symbol = s;
        auto itq = m_quotes.find(s);
        if (itq != m_quotes.end())
            q = itq->second;

        tip << s << L" | " << FormatPrice(q.last) << L" | " << FormatSignedPct(q.change_pct);
        if (!q.is_ok)
            tip << L" x";
        tip << L"\n";
        ++shown;
    }
    if ((int)symbols.size() > shown)
        tip << L"... +" << (symbols.size() - shown) << L" more\n";

    tip << L"Interval " << m_setting_data.update_interval_sec << L"s";
    m_render_cache.tooltip = tip.str();

    // 样例文本：用于宽度稳定；Phase 3 再按布局动态生成
    m_render_cache.sample = L"BTCUSDT 000000.00 +00.00%";
}

void CDataManager::UpdateRenderCache()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    RebuildRenderCacheLocked();
}

void CDataManager::UpdateMockQuotes()
{
    // 仅用于 Phase 1 验收：无网络时也能看到内容变化
    time_t now = time(nullptr);
    double seed = (double)(now % 1000) / 1000.0;

    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureDefaultsLocked();
    for (const auto& s : m_setting_data.symbols)
    {
        Quote q{};
        q.symbol = s;
        q.is_ok = true;
        q.update_time = now;

        // 简单可预测的模拟数值：不同 symbol 有不同基准
        double base = 100.0;
        if (s.find(L"BTC") == 0) base = 42000.0;
        else if (s.find(L"ETH") == 0) base = 2200.0;
        else if (s.find(L"SOL") == 0) base = 90.0;

        q.last = base * (1.0 + seed * 0.01);
        q.change_pct = (seed - 0.5) * 2.0; // [-1, +1]
        q.change = q.last * (q.change_pct / 100.0);
        q.high_24h = q.last * 1.02;
        q.low_24h = q.last * 0.98;
        q.volume_24h = base * 10.0;

        m_quotes[s] = q;
    }
    RebuildRenderCacheLocked();
}

void CDataManager::StepActiveSymbol(int delta)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureDefaultsLocked();
    if (m_setting_data.symbols.empty())
        return;

    size_t idx{};
    for (size_t i = 0; i < m_setting_data.symbols.size(); ++i)
    {
        if (_wcsicmp(m_setting_data.symbols[i].c_str(), m_setting_data.active_symbol.c_str()) == 0)
        {
            idx = i;
            break;
        }
    }

    int n = (int)m_setting_data.symbols.size();
    int next = ((int)idx + delta) % n;
    if (next < 0)
        next += n;
    m_setting_data.active_symbol = m_setting_data.symbols[(size_t)next];
    RebuildRenderCacheLocked();
}

std::pair<std::wstring, std::wstring> CDataManager::GetTaskbarLines() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return { m_render_cache.line1, m_render_cache.line2 };
}

std::wstring CDataManager::GetTooltipText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_render_cache.tooltip;
}

std::wstring CDataManager::GetSampleText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_render_cache.sample;
}

void CDataManager::SetRightAlign(bool right_align)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_right_align = right_align;
}

bool CDataManager::IsRightAlign() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_right_align;
}

void CDataManager::DebugLog(int level, const wchar_t* fmt, ...) const
{
    if (!m_setting_data.debug_log_enabled || level > m_setting_data.debug_log_level)
        return;

    wchar_t buffer[2048]{};
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, fmt, args);
    va_end(args);

    std::wstring line = buffer;
    line += L"\n";
    OutputDebugStringW(line.c_str());

    if (!m_log_path.empty())
    {
        std::ofstream file(m_log_path, std::ios::app);
        if (file)
        {
            int needed = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
            if (needed > 0)
            {
                std::string utf8;
                utf8.resize((size_t)needed);
                WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), utf8.data(), needed, nullptr, nullptr);
                file << utf8;
            }
        }
    }
}
