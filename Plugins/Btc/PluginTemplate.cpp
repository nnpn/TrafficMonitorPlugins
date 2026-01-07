#include "pch.h"
#include "PluginTemplate.h"
#include "DataManager.h"
#include "OptionsDlg.h"

CPluginTemplate CPluginTemplate::m_instance;

CPluginTemplate::CPluginTemplate()
{
    m_items[0].SetLineIndex(0);
    m_items[1].SetLineIndex(1);
}

CPluginTemplate& CPluginTemplate::Instance()
{
    return m_instance;
}

IPluginItem* CPluginTemplate::GetItem(int index)
{
    switch (index)
    {
    case 0:
        return &m_items[0];
    case 1:
        return &m_items[1];
    default:
        break;
    }
    return nullptr;
}

const wchar_t* CPluginTemplate::GetTooltipInfo()
{
    m_tooltip_info = g_data.GetTooltipText();
    return m_tooltip_info.c_str();
}

UINT CPluginTemplate::ThreadCallback(LPVOID)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CFlagLocker flag_locker(m_instance.m_is_thread_runing);

    m_instance.m_last_request_time = (unsigned __int64)time(nullptr);
    g_data.RequestRealtimeQuotes();
    return 0;
}

void CPluginTemplate::SendQuoteRequest()
{
    if (!m_is_thread_runing) // 确保线程已退出
        AfxBeginThread(ThreadCallback, nullptr);
}

void CPluginTemplate::DataRequired()
{
    // 轮询触发：根据刷新间隔请求一次报价更新；绘制与 Tooltip 读取缓存即可。
    time_t now = time(nullptr);
    int interval = g_data.GetEffectiveIntervalSec();
    if (!m_is_thread_runing && (now - (time_t)m_last_request_time) >= interval)
        SendQuoteRequest();
}

ITMPlugin::OptionReturn CPluginTemplate::ShowOptionsDialog(void* hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd* pParent = CWnd::FromHandle((HWND)hParent);
    COptionsDlg dlg(pParent);
    dlg.m_data = g_data.m_setting_data;
    if (dlg.DoModal() == IDOK)
    {
        g_data.m_setting_data = dlg.m_data;
        g_data.SaveConfig();
        return ITMPlugin::OR_OPTION_CHANGED;
    }
    return ITMPlugin::OR_OPTION_UNCHANGED;
}

const wchar_t* CPluginTemplate::GetInfo(PluginInfoIndex index)
{
    static CString str;
    switch (index)
    {
    case TMI_NAME:
        return g_data.StringRes(IDS_PLUGIN_NAME).GetString();
    case TMI_DESCRIPTION:
        return g_data.StringRes(IDS_PLUGIN_DESCRIPTION).GetString();
    case TMI_AUTHOR:
        return L"GPT-5.2";
    case TMI_COPYRIGHT:
        return L"Copyright (C) by GPT-5.2";
    case ITMPlugin::TMI_URL:
        return L"https://github.com/zhongyang219/TrafficMonitorPlugins";
    case TMI_VERSION:
        return L"0.1.0";
    default:
        break;
    }
    return L"";
}

void CPluginTemplate::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    switch (index)
    {
    case ITMPlugin::EI_CONFIG_DIR:
        //从配置文件读取配置
        g_data.LoadConfig(std::wstring(data));
        SendQuoteRequest();
        break;
    case ITMPlugin::EI_TASKBAR_WND_VALUE_RIGHT_ALIGN:
        g_data.SetRightAlign((_wtoi(data) != 0));
        break;
    default:
        break;
    }
}

int CPluginTemplate::GetCommandCount()
{
    // 注意：部分主程序版本不会将鼠标滚轮事件下发到插件区域，
    // 因此提供命令作为交互兜底（可在主程序右键菜单中访问）。
    return 7;
}

const wchar_t* CPluginTemplate::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case 0:
        return g_data.StringRes(IDS_COMMAND_UPDATE).GetString();
    case 1:
        return g_data.StringRes(IDS_COMMAND_NEXT_SYMBOL).GetString();
    case 2:
        return g_data.StringRes(IDS_COMMAND_PREV_SYMBOL).GetString();
    case 3:
        return g_data.StringRes(IDS_COMMAND_TOGGLE_LINE2).GetString();
    case 4:
        return g_data.StringRes(IDS_COMMAND_NEXT_DETAIL).GetString();
    case 5:
        return g_data.StringRes(IDS_COMMAND_PREV_DETAIL).GetString();
    case 6:
        return g_data.StringRes(IDS_COMMAND_TOGGLE_BOUNDS).GetString();
    default:
        break;
    }
    return nullptr;
}

void CPluginTemplate::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    switch (command_index)
    {
    case 0:
        SendQuoteRequest();
        break;
    case 1:
        g_data.StepActiveSymbol(1);
        break;
    case 2:
        g_data.StepActiveSymbol(-1);
        break;
    case 3:
        g_data.ToggleLine2Mode();
        break;
    case 4:
        if (g_data.IsLine2RollDetailMode())
            g_data.StepLine2Detail(1);
        else
            g_data.StepActiveSymbol(1);
        break;
    case 5:
        if (g_data.IsLine2RollDetailMode())
            g_data.StepLine2Detail(-1);
        else
            g_data.StepActiveSymbol(-1);
        break;
    case 6:
        g_data.ToggleDebugBounds();
        g_data.SaveConfig();
        break;
    default:
        break;
    }

    if (hWnd != nullptr)
        ::InvalidateRect((HWND)hWnd, nullptr, TRUE);
}

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CPluginTemplate::Instance();
}
