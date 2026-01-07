#include "pch.h"
#include "DataManager.h"
#include "../utilities/IniHelper.h"
#include "../utilities/Common.h"
#include "../utilities/JsonHelper.h"
#include "../utilities/yyjson/yyjson.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdarg>
#include <cmath>
#include <algorithm>
#include <afxinet.h>

#undef min
#undef max

namespace
{
    constexpr auto kUserAgent = L"TrafficMonitorPlugins-Btc/0.1";
    constexpr DWORD kInternetFlags = INTERNET_FLAG_TRANSFER_ASCII | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;

    bool FileExists(const std::wstring& path)
    {
        DWORD attr = GetFileAttributesW(path.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
    }

    bool WriteUtf8BomTextFile(const std::wstring& path, const std::wstring& content)
    {
        std::ofstream file(path, std::ios::binary);
        if (!file)
            return false;

        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        file.write((const char*)bom, sizeof(bom));

        std::string utf8 = utilities::StringHelper::UnicodeToStr(content.c_str(), true);
        file.write(utf8.data(), (std::streamsize)utf8.size());
        return true;
    }

    std::wstring BuildDefaultConfigTemplate()
    {
        // 注：ini 使用 UTF-8(BOM) 保存；注释行以 ';' 开头。
        std::wstringstream wss;
        wss
            << L"; Btc plugin config (auto-generated)\n"
            << L";\n"
            << L"; symbols: 监控列表，格式为英文逗号分隔且每项带引号：\"BTCUSDT\",\"ETHUSDT\"\n"
            << L"; active_symbol: 任务栏聚焦币种（必须是 symbols 里的一个）\n"
            << L"; update_interval_sec: 报价刷新间隔（秒）\n"
            << L"; tooltip_max_coins: Tooltip 概览最多展示币种数量\n"
            << L"; stale_threshold_sec: 超过该秒数未更新则标记为过期(*)\n"
            << L"; line2_mode: 第二行显示模式：dual_symbol / roll_detail\n"
            << L"; line2_roll_items: 当 line2_mode=roll_detail 时第二行可滚动显示的明细项\n"
            << L"; color_with_change: 按涨跌给整行文本着色（true/false）\n"
            << L"; up_is_red: true=涨红跌绿；false=涨绿跌红\n"
            << L"\n"
            << L"[config]\n"
            << L"symbols = \"BTCUSDT\",\"ETHUSDT\",\"SOLUSDT\"\n"
            << L"active_symbol = \"BTCUSDT\"\n"
            << L"update_interval_sec = 5\n"
            << L"tooltip_max_coins = 8\n"
            << L"stale_threshold_sec = 30\n"
            << L"; 兼容旧字段：second_line_dual_symbol=true 等价于 line2_mode=dual_symbol\n"
            << L"second_line_dual_symbol = true\n"
            << L"line2_mode = dual_symbol\n"
            << L"line2_roll_items = \"high_low\",\"volume\",\"bid_ask\",\"update_time\"\n"
            << L"color_with_change = false\n"
            << L"up_is_red = true\n"
            << L"\n"
            << L"[debug]\n"
            << L"; log_enabled: 输出调试日志到 <dllname>.log\n"
            << L"log_enabled = false\n"
            << L"log_level = 1\n"
            << L"; dump_last_response: 保存最近一次 HTTP 响应到 <dllname>.last_response.json\n"
            << L"dump_last_response = false\n"
            << L"; show_bounds: 在任务栏绘制边框辅助调试\n"
            << L"show_bounds = false\n";
        return wss.str();
    }

    bool HttpGet(const std::wstring& url, std::string& out, std::wstring& error)
    {
        out.clear();
        error.clear();

        CInternetSession session(kUserAgent);
        session.SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, 5000);
        session.SetOption(INTERNET_OPTION_SEND_TIMEOUT, 5000);
        session.SetOption(INTERNET_OPTION_RECEIVE_TIMEOUT, 5000);

        CHttpFile* file = nullptr;
        try
        {
            file = (CHttpFile*)session.OpenURL(url.c_str(), 1, kInternetFlags);
            DWORD status{};
            file->QueryInfoStatusCode(status);
            if (status != HTTP_STATUS_OK)
            {
                error = L"HTTP ";
                error += std::to_wstring(status);
                file->Close();
                delete file;
                session.Close();
                return false;
            }

            // 使用二进制读取避免 Unicode/ANSI CString 转换导致的编码问题
            char buf[4096];
            UINT nRead{};
            while ((nRead = file->Read(buf, sizeof(buf))) > 0)
            {
                out.append(buf, buf + nRead);
            }
            file->Close();
            delete file;
            session.Close();
            return true;
        }
        catch (CInternetException* e)
        {
            wchar_t msg[256]{};
            e->GetErrorMessage(msg, _countof(msg));
            error = msg;
            e->Delete();
        }

        if (file != nullptr)
        {
            file->Close();
            delete file;
        }
        session.Close();
        return false;
    }

    double ParseDouble(const std::string& s)
    {
        if (s.empty())
            return 0.0;
        char* end{};
        double v = strtod(s.c_str(), &end);
        return (end == s.c_str()) ? 0.0 : v;
    }

    bool ParseBinance24hr(const std::string& json, std::map<std::wstring, Quote>& out, std::wstring& error)
    {
        out.clear();
        error.clear();
        yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
        if (doc == nullptr)
        {
            error = L"json parse failed";
            return false;
        }
        yyjson_val* root = yyjson_doc_get_root(doc);
        if (root == nullptr || !yyjson_is_arr(root))
        {
            yyjson_doc_free(doc);
            error = L"json root not array";
            return false;
        }

        yyjson_val* val{};
        size_t idx{}, max{};
        yyjson_arr_foreach(root, idx, max, val)
        {
            if (val == nullptr || !yyjson_is_obj(val))
                continue;

            std::string symbol = utilities::JsonHelper::GetJsonString(val, "symbol");
            if (symbol.empty())
                continue;

            Quote q{};
            q.symbol = utilities::StringHelper::StrToUnicode(symbol.c_str(), true);
            q.last = ParseDouble(utilities::JsonHelper::GetJsonString(val, "lastPrice"));
            q.change = ParseDouble(utilities::JsonHelper::GetJsonString(val, "priceChange"));
            q.change_pct = ParseDouble(utilities::JsonHelper::GetJsonString(val, "priceChangePercent"));
            q.high_24h = ParseDouble(utilities::JsonHelper::GetJsonString(val, "highPrice"));
            q.low_24h = ParseDouble(utilities::JsonHelper::GetJsonString(val, "lowPrice"));
            q.volume_24h = ParseDouble(utilities::JsonHelper::GetJsonString(val, "volume"));
            q.bid = ParseDouble(utilities::JsonHelper::GetJsonString(val, "bidPrice"));
            q.ask = ParseDouble(utilities::JsonHelper::GetJsonString(val, "askPrice"));
            q.is_ok = (q.last > 0.0);

            yyjson_val* close_time = yyjson_obj_get(val, "closeTime");
            if (close_time != nullptr && yyjson_is_int(close_time))
            {
                long long ms = yyjson_get_sint(close_time);
                q.update_time = (time_t)(ms / 1000);
            }
            else
            {
                q.update_time = time(nullptr);
            }
            out[q.symbol] = q;
        }

        yyjson_doc_free(doc);
        if (out.empty())
        {
            error = L"empty quotes";
            return false;
        }
        return true;
    }

    std::wstring UrlEncodeSymbolsParam(const std::vector<std::wstring>& symbols)
    {
        // 构造 Binance symbols 参数：["BTCUSDT","ETHUSDT"] 并做最小 URL 编码
        std::wstring json = L"[";
        for (size_t i = 0; i < symbols.size(); ++i)
        {
            if (i > 0)
                json += L",";
            json += L"\"";
            json += symbols[i];
            json += L"\"";
        }
        json += L"]";

        std::wstring encoded;
        encoded.reserve(json.size() * 3);
        for (wchar_t ch : json)
        {
            switch (ch)
            {
            case L'[': encoded += L"%5B"; break;
            case L']': encoded += L"%5D"; break;
            case L'"': encoded += L"%22"; break;
            case L',': encoded += L"%2C"; break;
            case L' ': encoded += L"%20"; break;
            default:
                encoded.push_back(ch);
                break;
            }
        }
        return encoded;
    }
}

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
    m_last_response_path = base_dir + module_file_name + L".last_response.json";

    // 首次运行自动生成模板（带注释），便于直接修改
    if (!FileExists(m_config_path))
    {
        WriteUtf8BomTextFile(m_config_path, BuildDefaultConfigTemplate());
    }

    utilities::CIniHelper ini(m_config_path);
    if (ini.IsEmpty())
    {
        // 文件存在但为空/不可读时重建模板
        WriteUtf8BomTextFile(m_config_path, BuildDefaultConfigTemplate());
        utilities::CIniHelper ini2(m_config_path);
        ini2.GetStringList(L"config", L"symbols", m_setting_data.symbols, std::vector<std::wstring>{});
        m_setting_data.active_symbol = ini2.GetString(L"config", L"active_symbol", L"");
        m_setting_data.update_interval_sec = ini2.GetInt(L"config", L"update_interval_sec", 5);
        m_setting_data.tooltip_max_coins = ini2.GetInt(L"config", L"tooltip_max_coins", 8);
        m_setting_data.stale_threshold_sec = ini2.GetInt(L"config", L"stale_threshold_sec", 30);
        // 第二行模式：优先新字段，其次兼容旧字段
        std::wstring mode = ini2.GetString(L"config", L"line2_mode", L"");
        if (_wcsicmp(mode.c_str(), L"roll_detail") == 0)
            m_setting_data.line2_mode = SettingData::Line2Mode::RollDetail;
        else if (_wcsicmp(mode.c_str(), L"dual_symbol") == 0)
            m_setting_data.line2_mode = SettingData::Line2Mode::DualSymbol;
        else
            m_setting_data.line2_mode = ini2.GetBool(L"config", L"second_line_dual_symbol", true) ? SettingData::Line2Mode::DualSymbol : SettingData::Line2Mode::RollDetail;
        ini2.GetStringList(L"config", L"line2_roll_items", m_setting_data.line2_roll_items, std::vector<std::wstring>{});
        m_setting_data.color_with_change = ini2.GetBool(L"config", L"color_with_change", false);
        m_setting_data.up_is_red = ini2.GetBool(L"config", L"up_is_red", true);
        m_setting_data.debug_log_enabled = ini2.GetBool(L"debug", L"log_enabled", false);
        m_setting_data.debug_log_level = ini2.GetInt(L"debug", L"log_level", 1);
        m_setting_data.debug_dump_last_response = ini2.GetBool(L"debug", L"dump_last_response", false);
        m_setting_data.debug_show_bounds = ini2.GetBool(L"debug", L"show_bounds", false);
    }
    else
    {
    ini.GetStringList(L"config", L"symbols", m_setting_data.symbols, std::vector<std::wstring>{});
    m_setting_data.active_symbol = ini.GetString(L"config", L"active_symbol", L"");
    m_setting_data.update_interval_sec = ini.GetInt(L"config", L"update_interval_sec", 5);
    m_setting_data.tooltip_max_coins = ini.GetInt(L"config", L"tooltip_max_coins", 8);
    m_setting_data.stale_threshold_sec = ini.GetInt(L"config", L"stale_threshold_sec", 30);
    std::wstring mode = ini.GetString(L"config", L"line2_mode", L"");
    if (_wcsicmp(mode.c_str(), L"roll_detail") == 0)
        m_setting_data.line2_mode = SettingData::Line2Mode::RollDetail;
    else if (_wcsicmp(mode.c_str(), L"dual_symbol") == 0)
        m_setting_data.line2_mode = SettingData::Line2Mode::DualSymbol;
    else
        m_setting_data.line2_mode = ini.GetBool(L"config", L"second_line_dual_symbol", true) ? SettingData::Line2Mode::DualSymbol : SettingData::Line2Mode::RollDetail;
    ini.GetStringList(L"config", L"line2_roll_items", m_setting_data.line2_roll_items, std::vector<std::wstring>{});
    m_setting_data.color_with_change = ini.GetBool(L"config", L"color_with_change", false);
    m_setting_data.up_is_red = ini.GetBool(L"config", L"up_is_red", true);
    m_setting_data.debug_log_enabled = ini.GetBool(L"debug", L"log_enabled", false);
    m_setting_data.debug_log_level = ini.GetInt(L"debug", L"log_level", 1);
    m_setting_data.debug_dump_last_response = ini.GetBool(L"debug", L"dump_last_response", false);
    m_setting_data.debug_show_bounds = ini.GetBool(L"debug", L"show_bounds", false);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        EnsureDefaultsLocked();
    }

    // 初始化缓存（避免首次绘制为空）
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& s : m_setting_data.symbols)
        {
            if (m_quotes.find(s) == m_quotes.end())
            {
                Quote q{};
                q.symbol = s;
                q.is_ok = false;
                q.error = L"loading";
                q.update_time = time(nullptr);
                m_quotes[s] = q;
            }
        }
        RebuildRenderCacheLocked();
    }
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
        const bool dual = (m_setting_data.line2_mode == SettingData::Line2Mode::DualSymbol);
        ini.WriteBool(L"config", L"second_line_dual_symbol", dual);
        ini.WriteString(L"config", L"line2_mode", dual ? L"dual_symbol" : L"roll_detail");
        ini.WriteStringList(L"config", L"line2_roll_items", m_setting_data.line2_roll_items);
        ini.WriteBool(L"config", L"color_with_change", m_setting_data.color_with_change);
        ini.WriteBool(L"config", L"up_is_red", m_setting_data.up_is_red);
        ini.WriteBool(L"debug", L"log_enabled", m_setting_data.debug_log_enabled);
        ini.WriteInt(L"debug", L"log_level", m_setting_data.debug_log_level);
        ini.WriteBool(L"debug", L"dump_last_response", m_setting_data.debug_dump_last_response);
        ini.WriteBool(L"debug", L"show_bounds", m_setting_data.debug_show_bounds);
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

    if (m_setting_data.line2_roll_items.empty())
        m_setting_data.line2_roll_items = { L"high_low", L"volume", L"bid_ask", L"update_time" };

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

    // roll_detail 索引合法化
    auto items = GetLine2DetailItemsLocked();
    if (items.empty())
        m_line2_detail_index = 0;
    else
    {
        int n = (int)items.size();
        if (m_line2_detail_index < 0)
            m_line2_detail_index = 0;
        if (m_line2_detail_index >= n)
            m_line2_detail_index = n - 1;
    }
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

    // 过期标记：避免影响数值解析，使用末尾 '*'
    time_t now = time(nullptr);
    if (quote.update_time > 0 && (now - quote.update_time) > m_setting_data.stale_threshold_sec)
        line += L" *";
    return line;
}

std::wstring CDataManager::FormatCompactNumber(double value) const
{
    if (value <= 0.0)
        return L"--";

    const double abs_v = std::fabs(value);
    const wchar_t* suffix = L"";
    double v = abs_v;
    if (abs_v >= 1e9)
    {
        v = abs_v / 1e9;
        suffix = L"B";
    }
    else if (abs_v >= 1e6)
    {
        v = abs_v / 1e6;
        suffix = L"M";
    }
    else if (abs_v >= 1e3)
    {
        v = abs_v / 1e3;
        suffix = L"K";
    }

    int decimals = (v >= 100.0) ? 0 : ((v >= 10.0) ? 1 : 2);
    std::wstringstream wss;
    wss << std::fixed << std::setprecision(decimals) << v << suffix;
    return wss.str();
}

std::wstring CDataManager::FormatClockTime(time_t t) const
{
    if (t <= 0)
        return L"--:--:--";
    tm tm_local{};
    localtime_s(&tm_local, &t);
    wchar_t buf[32]{};
    wcsftime(buf, _countof(buf), L"%H:%M:%S", &tm_local);
    return buf;
}

int CDataManager::TrendFromQuoteLocked(const Quote& quote) const
{
    if (!quote.is_ok)
        return 0;
    if (quote.change_pct > 0.0)
        return 1;
    if (quote.change_pct < 0.0)
        return -1;
    return 0;
}

Quote CDataManager::GetQuoteLocked(const std::wstring& symbol) const
{
    Quote q{};
    q.symbol = symbol;
    auto it = m_quotes.find(symbol);
    if (it != m_quotes.end())
        q = it->second;
    return q;
}

bool CDataManager::TryParseDetailItem(const std::wstring& token, Line2DetailItem& out) const
{
    if (_wcsicmp(token.c_str(), L"high_low") == 0 || _wcsicmp(token.c_str(), L"hl") == 0)
    {
        out = Line2DetailItem::HighLow;
        return true;
    }
    if (_wcsicmp(token.c_str(), L"volume") == 0 || _wcsicmp(token.c_str(), L"vol") == 0)
    {
        out = Line2DetailItem::Volume;
        return true;
    }
    if (_wcsicmp(token.c_str(), L"bid_ask") == 0 || _wcsicmp(token.c_str(), L"bidask") == 0)
    {
        out = Line2DetailItem::BidAsk;
        return true;
    }
    if (_wcsicmp(token.c_str(), L"update_time") == 0 || _wcsicmp(token.c_str(), L"time") == 0 || _wcsicmp(token.c_str(), L"updated") == 0)
    {
        out = Line2DetailItem::UpdateTime;
        return true;
    }
    return false;
}

std::vector<CDataManager::Line2DetailItem> CDataManager::GetLine2DetailItemsLocked() const
{
    std::vector<Line2DetailItem> items;
    for (const auto& token : m_setting_data.line2_roll_items)
    {
        if (token.empty())
            continue;
        Line2DetailItem item{};
        if (TryParseDetailItem(token, item))
            items.push_back(item);
    }
    if (items.empty())
        items = { Line2DetailItem::HighLow, Line2DetailItem::Volume, Line2DetailItem::BidAsk, Line2DetailItem::UpdateTime };
    return items;
}

std::wstring CDataManager::FormatDetailLineLocked(const Quote& quote) const
{
    if (!quote.is_ok)
    {
        std::wstring line = L"--";
        if (!quote.error.empty())
        {
            line += L" (";
            line += quote.error;
            line += L')';
        }
        return line;
    }

    auto items = GetLine2DetailItemsLocked();
    if (items.empty())
        return L"--";

    int idx = m_line2_detail_index;
    if (idx < 0) idx = 0;
    if (idx >= (int)items.size()) idx = (int)items.size() - 1;
    const Line2DetailItem item = items[(size_t)idx];

    std::wstringstream wss;
    switch (item)
    {
    case Line2DetailItem::HighLow:
        wss << L"H/L " << FormatPrice(quote.high_24h) << L"/" << FormatPrice(quote.low_24h);
        break;
    case Line2DetailItem::Volume:
        wss << L"VOL " << FormatCompactNumber(quote.volume_24h);
        break;
    case Line2DetailItem::BidAsk:
        if (quote.bid > 0.0 && quote.ask > 0.0)
            wss << L"B/A " << FormatPrice(quote.bid) << L"/" << FormatPrice(quote.ask);
        else
            wss << L"B/A --";
        break;
    case Line2DetailItem::UpdateTime:
    default:
    {
        time_t now = time(nullptr);
        int age = (quote.update_time > 0) ? (int)(now - quote.update_time) : -1;
        wss << L"UPD " << FormatClockTime(quote.update_time);
        if (age >= 0)
            wss << L" (" << age << L"s)";
        break;
    }
    }

    time_t now = time(nullptr);
    if (quote.update_time > 0 && (now - quote.update_time) > m_setting_data.stale_threshold_sec)
        wss << L" *";
    return wss.str();
}

void CDataManager::RebuildRenderCacheLocked()
{
    EnsureDefaultsLocked();

    const auto& symbols = m_setting_data.symbols;
    const std::wstring& active = m_setting_data.active_symbol;

    Quote active_quote = GetQuoteLocked(active);

    m_render_cache.line1 = FormatCoreLineLocked(active_quote);
    m_render_cache.trend1 = TrendFromQuoteLocked(active_quote);

    // 第二行：按配置二选一（dual_symbol / roll_detail）
    Quote line2_quote{};
    if (m_setting_data.line2_mode == SettingData::Line2Mode::RollDetail)
    {
        m_render_cache.line2 = FormatDetailLineLocked(active_quote);
        m_render_cache.trend2 = m_render_cache.trend1;
    }
    else
    {
        // dual_symbol：显示下一个币种核心行，形成多币种扫视
        if (symbols.size() <= 1)
        {
            // 单币种时避免“同一行重复”，退化为显示当前币种明细
            m_render_cache.line2 = FormatDetailLineLocked(active_quote);
            m_render_cache.trend2 = m_render_cache.trend1;
        }
        else
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
            line2_quote = GetQuoteLocked(symbols[next_index]);
            m_render_cache.line2 = FormatCoreLineLocked(line2_quote);
            m_render_cache.trend2 = TrendFromQuoteLocked(line2_quote);
        }
    }

    m_render_cache.color_with_change = m_setting_data.color_with_change;
    m_render_cache.up_is_red = m_setting_data.up_is_red;
    m_render_cache.debug_show_bounds = m_setting_data.debug_show_bounds;

    // Tooltip：多币种概览 + 状态行（Phase 4 完善）
    std::wstringstream tip;
    int shown = 0;
    int max_show = m_setting_data.tooltip_max_coins;
    time_t now = time(nullptr);
    // active_symbol 优先置顶，便于在多币种下快速定位
    std::vector<std::wstring> ordered;
    ordered.reserve(symbols.size());
    if (!active.empty())
        ordered.push_back(active);
    for (const auto& s : symbols)
    {
        if (!active.empty() && _wcsicmp(s.c_str(), active.c_str()) == 0)
            continue;
        ordered.push_back(s);
    }

    for (const auto& s : ordered)
    {
        if (shown >= max_show)
            break;
        Quote q = GetQuoteLocked(s);

        if (!active.empty() && _wcsicmp(s.c_str(), active.c_str()) == 0)
            tip << L"> ";
        tip << s << L" | " << FormatPrice(q.last) << L" | " << FormatSignedPct(q.change_pct);
        if (!q.is_ok)
            tip << L" x";
        else if (q.update_time > 0 && (now - q.update_time) > m_setting_data.stale_threshold_sec)
            tip << L" *";
        tip << L"\n";
        ++shown;
    }
    if ((int)ordered.size() > shown)
        tip << L"... +" << (ordered.size() - shown) << L" more\n";

    tip << L"Updated " << FormatClockTime(m_last_success_time)
        << L" | Interval " << m_setting_data.update_interval_sec << L"s";
    if (m_backoff_sec > 0)
        tip << L" | Backoff " << m_backoff_sec << L"s";
    tip << L" | Line2 " << (m_setting_data.line2_mode == SettingData::Line2Mode::DualSymbol ? L"dual" : L"detail");
    m_render_cache.tooltip = tip.str();

    // 样例文本：用于宽度稳定；Phase 3 再按布局动态生成
    m_render_cache.sample = L"BTCUSDT 000000.00 +00.00%";
}

int CDataManager::GetUpdateIntervalSec() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int sec = m_setting_data.update_interval_sec;
    return (sec < 1) ? 1 : sec;
}

int CDataManager::GetEffectiveIntervalSec() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int base = m_setting_data.update_interval_sec;
    if (base < 1)
        base = 1;
    return (std::max)(base, m_backoff_sec);
}

bool CDataManager::RequestRealtimeQuotes()
{
    std::vector<std::wstring> symbols;
    bool dump_last_response{};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        EnsureDefaultsLocked();
        symbols = m_setting_data.symbols;
        dump_last_response = m_setting_data.debug_dump_last_response;
    }
    if (symbols.empty())
        return false;

    std::wstring url = L"https://api.binance.com/api/v3/ticker/24hr?symbols=";
    url += UrlEncodeSymbolsParam(symbols);

    std::string body;
    std::wstring http_err;
    if (!HttpGet(url, body, http_err))
    {
        DebugLog(1, L"HttpGet failed: %s", http_err.c_str());
        std::lock_guard<std::mutex> lock(m_mutex);
        m_backoff_sec = (std::min)(60, (m_backoff_sec == 0 ? 10 : m_backoff_sec * 2));
        // 标记失败但不清空历史有效数据（让 Tooltip 可观察错误）
        Quote err{};
        err.symbol = m_setting_data.active_symbol;
        err.is_ok = false;
        err.error = http_err;
        err.update_time = time(nullptr);
        m_quotes[err.symbol] = err;
        RebuildRenderCacheLocked();
        return false;
    }

    std::map<std::wstring, Quote> parsed;
    std::wstring parse_err;

    // 调试：落盘保存最近一次响应，便于排查字段变化/限流等问题
    if (dump_last_response && !m_last_response_path.empty())
    {
        std::ofstream file(m_last_response_path, std::ios::binary);
        if (file)
        {
            file.write(body.data(), (std::streamsize)body.size());
        }
    }

    if (!ParseBinance24hr(body, parsed, parse_err))
    {
        DebugLog(1, L"ParseBinance24hr failed: %s", parse_err.c_str());
        std::lock_guard<std::mutex> lock(m_mutex);
        m_backoff_sec = (std::min)(60, (m_backoff_sec == 0 ? 10 : m_backoff_sec * 2));
        Quote err{};
        err.symbol = m_setting_data.active_symbol;
        err.is_ok = false;
        err.error = parse_err;
        err.update_time = time(nullptr);
        m_quotes[err.symbol] = err;
        RebuildRenderCacheLocked();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_backoff_sec = 0;
        m_last_success_time = time(nullptr);

        // 更新/合并：不在返回里的 symbol 标记为失败
        for (const auto& s : symbols)
        {
            auto it = parsed.find(s);
            if (it != parsed.end())
            {
                m_quotes[s] = it->second;
            }
            else
            {
                Quote q{};
                q.symbol = s;
                q.is_ok = false;
                q.error = L"no data";
                q.update_time = time(nullptr);
                m_quotes[s] = q;
            }
        }
        RebuildRenderCacheLocked();
    }
    return true;
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
        q.bid = q.last * 0.999;
        q.ask = q.last * 1.001;

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

void CDataManager::StepLine2Detail(int delta)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureDefaultsLocked();
    if (m_setting_data.line2_mode != SettingData::Line2Mode::RollDetail)
        return;

    auto items = GetLine2DetailItemsLocked();
    if (items.size() <= 1)
        return;

    int n = (int)items.size();
    int next = (m_line2_detail_index + delta) % n;
    if (next < 0)
        next += n;
    m_line2_detail_index = next;
    RebuildRenderCacheLocked();
}

bool CDataManager::IsLine2RollDetailMode() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (m_setting_data.line2_mode == SettingData::Line2Mode::RollDetail);
}

std::pair<std::wstring, std::wstring> CDataManager::GetTaskbarLines() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return { m_render_cache.line1, m_render_cache.line2 };
}

CDataManager::TaskbarLinesEx CDataManager::GetTaskbarLinesEx() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    TaskbarLinesEx ex{};
    ex.line1 = m_render_cache.line1;
    ex.line2 = m_render_cache.line2;
    ex.trend1 = m_render_cache.trend1;
    ex.trend2 = m_render_cache.trend2;
    ex.color_with_change = m_render_cache.color_with_change;
    ex.up_is_red = m_render_cache.up_is_red;
    ex.debug_show_bounds = m_render_cache.debug_show_bounds;
    ex.right_align = m_right_align;
    return ex;
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
                WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), &utf8[0], needed, nullptr, nullptr);
                file << utf8;
            }
        }
    }
}
