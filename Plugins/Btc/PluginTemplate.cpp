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

void CPluginTemplate::DataRequired()
{
    // Phase 1：仅使用模拟数据刷新渲染缓存，确保 UI 行为可验收。
    static time_t last_tick{};
    time_t now = time(nullptr);
    if (now != last_tick)
    {
        last_tick = now;
        g_data.UpdateMockQuotes();
        m_tooltip_info = g_data.GetTooltipText();
    }
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
        m_tooltip_info = g_data.GetTooltipText();
        break;
    case ITMPlugin::EI_TASKBAR_WND_VALUE_RIGHT_ALIGN:
        g_data.SetRightAlign((_wtoi(data) != 0));
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
