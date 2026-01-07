#pragma once
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <ctime>
#include "resource.h"

#define g_data CDataManager::Instance()

/**
 * @brief 单个币种/交易对的快照数据
 *
 * 说明：
 * - 该结构作为“单币种对象模板”，可扩展到 1~N 个币种统一渲染。
 * - UI 线程只读取缓存；后台线程更新该结构并生成渲染缓存。
 */
struct Quote
{
    std::wstring symbol;            // 例如：BTCUSDT
    double last{};                  // 最新价
    double change{};                // 24h 涨跌额
    double change_pct{};            // 24h 涨跌幅（百分比，+1.23 表示 +1.23%）
    double high_24h{};
    double low_24h{};
    double volume_24h{};
    time_t update_time{};           // 数据时间（UTC/本地均可，Phase 2 决定）
    bool is_ok{};                   // 本次快照是否有效
    std::wstring error;             // 失败原因摘要（仅 is_ok=false 时有效）
};

/**
 * @brief K 线/采样点（用于 24h(UTC) 固定窗口迷你图，Phase 6）
 */
struct Candle
{
    time_t start_ts_utc{};
    double open{};
    double high{};
    double low{};
    double close{};
    double volume{};
};

struct SettingData
{
    // 监控列表（1~N）
    std::vector<std::wstring> symbols;

    // 当前任务栏聚焦币种（优先匹配 symbols 中的元素）
    std::wstring active_symbol;

    // 报价刷新间隔（秒）
    int update_interval_sec{ 5 };

    // Tooltip 最大展示币种数量
    int tooltip_max_coins{ 8 };

    // 过期阈值（秒）：超过该时间未更新则标记为 stale
    int stale_threshold_sec{ 30 };

    // 第二行显示模式：true=双币同屏（显示下一个币种核心行），false=当前币种滚动明细（Phase 3）
    bool second_line_dual_symbol{ true };

    // Debug 开关
    bool debug_log_enabled{ false };
    int debug_log_level{ 1 };
};

class CDataManager
{
private:
    CDataManager();
    ~CDataManager();

public:
    static CDataManager& Instance();

    void LoadConfig(const std::wstring& config_dir);
    void SaveConfig() const;
    const CString& StringRes(UINT id);      //根据资源id获取一个字符串资源
    void DPIFromWindow(CWnd* pWnd);
    int DPI(int pixel);
    float DPIF(float pixel);
    int RDPI(int pixel);
    HICON GetIcon(UINT id);

    /**
     * @brief 更新内部渲染缓存（仅基于当前缓存数据，不做网络请求）
     */
    void UpdateRenderCache();

    /**
     * @brief 使用模拟数据填充缓存（Phase 1 验收用）
     */
    void UpdateMockQuotes();

    /**
     * @brief 将 active_symbol 前后滚动（循环）
     */
    void StepActiveSymbol(int delta);

    /**
     * @brief 获取当前可绘制的两行文本（线程安全快照）
     */
    std::pair<std::wstring, std::wstring> GetTaskbarLines() const;

    /**
     * @brief 获取 Tooltip 文本（线程安全快照）
     */
    std::wstring GetTooltipText() const;

    /**
     * @brief 获取宽度估算用的样例文本（线程安全快照）
     */
    std::wstring GetSampleText() const;

    /**
     * @brief 设置任务栏数值是否右对齐（主程序回传）
     */
    void SetRightAlign(bool right_align);

    /**
     * @brief 返回是否数值右对齐（用于 DrawItem）
     */
    bool IsRightAlign() const;

    /**
     * @brief 记录 debug 日志（可开关）
     */
    void DebugLog(int level, const wchar_t* fmt, ...) const;

    /**
     * @brief 拉取并更新报价缓存（网络请求）
     * @return 成功返回 true；失败返回 false，失败原因可从 Tooltip/日志观察
     */
    bool RequestRealtimeQuotes();

    /**
     * @brief 获取报价刷新间隔（秒）
     */
    int GetUpdateIntervalSec() const;

    /**
     * @brief 获取实际刷新间隔（秒）：包含失败退避
     */
    int GetEffectiveIntervalSec() const;

    SettingData m_setting_data;

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    std::wstring m_log_path;
    std::map<UINT, CString> m_string_table;
    std::map<UINT, HICON> m_icons;
    int m_dpi{ 96 };

    // UI 绘制相关（线程安全快照）
    struct RenderCache
    {
        std::wstring line1;
        std::wstring line2;
        std::wstring tooltip;
        std::wstring sample;
    };

    void EnsureDefaultsLocked();
    void RebuildRenderCacheLocked();
    std::wstring FormatCoreLineLocked(const Quote& quote) const;
    std::wstring FormatPrice(double price) const;
    std::wstring FormatSignedPct(double pct) const;

    mutable std::mutex m_mutex;
    std::map<std::wstring, Quote> m_quotes;
    RenderCache m_render_cache;
    bool m_right_align{};

    // 请求与失败退避（Phase 2）
    time_t m_last_success_time{};
    int m_backoff_sec{};
};
