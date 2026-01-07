#include "pch.h"
#include "PluginTemplate.h"
#include "DataManager.h"
#include "OptionsDlg.h"

CPluginTemplate CPluginTemplate::m_instance;

CPluginTemplate::CPluginTemplate()
{
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
        return &m_item;
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
    int interval = g_data.GetUpdateIntervalSec();
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
    return 1;
}

const wchar_t* CPluginTemplate::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case 0:
        return g_data.StringRes(IDS_COMMAND_UPDATE).GetString();
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
    default:
        break;
    }
}

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CPluginTemplate::Instance();
}
