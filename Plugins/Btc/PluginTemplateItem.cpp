#include "pch.h"
#include "PluginTemplateItem.h"
#include "DataManager.h"
#include <algorithm>
#undef min
#undef max

void CPluginTemplateItem::SetLineIndex(int line_index)
{
    m_line_index = line_index;
}

const wchar_t* CPluginTemplateItem::GetItemName() const
{
    if (m_line_index == 0)
        m_name_cache = g_data.StringRes(IDS_PLUGIN_ITEM_NAME_LINE1).GetString();
    else
        m_name_cache = g_data.StringRes(IDS_PLUGIN_ITEM_NAME_LINE2).GetString();
    return m_name_cache.c_str();
}

const wchar_t* CPluginTemplateItem::GetItemId() const
{
    m_id_cache = L"BtcPrice01";
    m_id_cache += (m_line_index == 0 ? L"A" : L"B");
    return m_id_cache.c_str();
}

const wchar_t* CPluginTemplateItem::GetItemLableText() const
{
    return L"";
}

const wchar_t* CPluginTemplateItem::GetItemValueText() const
{
    return L"";
}

const wchar_t* CPluginTemplateItem::GetItemValueSampleText() const
{
    m_sample_cache = g_data.GetSampleText();
    return m_sample_cache.c_str();
}

bool CPluginTemplateItem::IsCustomDraw() const
{
    return true;
}

int CPluginTemplateItem::GetItemWidthEx(void * hDC) const
{
    CDC* pDC = CDC::FromHandle((HDC)hDC);
    auto lines = g_data.GetTaskbarLines();
    std::wstring t1 = L" ";
    t1 += lines.first;
    std::wstring t2 = L" ";
    t2 += lines.second;
    int w1 = pDC->GetTextExtent(t1.c_str()).cx;
    int w2 = pDC->GetTextExtent(t2.c_str()).cx;
    return std::max(w1, w2);
}

void CPluginTemplateItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    CDC* pDC = CDC::FromHandle((HDC)hDC);
    CRect rect(CPoint(x, y), CSize(w, h));

    const auto snap = g_data.GetTaskbarLinesEx();
    const std::wstring& text = (m_line_index == 0 ? snap.line1 : snap.line2);
    const int trend = (m_line_index == 0 ? snap.trend1 : snap.trend2);

    COLORREF color_default;
    COLORREF color_red;
    COLORREF color_green;
    if (dark_mode)
    {
        color_default = RGB(255, 255, 255);
        color_red = RGB(255, 121, 120);
        color_green = RGB(111, 215, 149);
    }
    else
    {
        color_default = RGB(0, 0, 0);
        color_red = RGB(195, 0, 0);
        color_green = RGB(46, 139, 87);
    }

    COLORREF text_color = color_default;
    if (snap.color_with_change && trend != 0)
    {
        const bool up = (trend > 0);
        const bool up_is_red = snap.up_is_red;
        const bool use_red = (up ? up_is_red : !up_is_red);
        text_color = use_red ? color_red : color_green;
    }
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(text_color);

    UINT flags = DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    if (snap.right_align)
        flags |= DT_RIGHT;

    // 视觉间距：统一使用一个前导空格，避免过近，同时不引入额外尾部留白
    std::wstring draw = L" ";
    draw += text;
    pDC->DrawText(draw.c_str(), rect, flags | DT_VCENTER);

    if (snap.debug_show_bounds)
    {
        COLORREF border = dark_mode ? RGB(80, 160, 255) : RGB(0, 120, 215);
        pDC->Draw3dRect(rect, border, border);
    }
}

int CPluginTemplateItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
{
    const bool line2_roll_detail = (m_line_index == 1) && g_data.IsLine2RollDetailMode();
    const bool ctrl_down = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift_down = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (type)
    {
    case IPluginItem::MT_LCLICKED:
        // 兼容：如果主程序不下发滚轮事件，至少可以用点击切换币种
        g_data.DebugLog(2, L"LClick line=%d flag=%d", m_line_index, flag);
        g_data.StepActiveSymbol(shift_down ? -1 : 1);
        ::InvalidateRect((HWND)hWnd, nullptr, TRUE);
        return 1;
    case IPluginItem::MT_DBCLICKED:
        // 快捷切换第二行模式（不持久化）
        g_data.DebugLog(2, L"DBClick line=%d flag=%d", m_line_index, flag);
        g_data.ToggleLine2Mode();
        ::InvalidateRect((HWND)hWnd, nullptr, TRUE);
        return 1;
    case IPluginItem::MT_WHEEL_UP:
    {
        auto before = g_data.GetTaskbarLinesEx();
        // 默认：滚轮切换币种（多币种滚动列表）
        // Ctrl + 滚轮：当第二行处于 roll_detail 模式时切换明细项
        if (line2_roll_detail && ctrl_down)
            g_data.StepLine2Detail(-1);
        else
            g_data.StepActiveSymbol(-1);
        auto after = g_data.GetTaskbarLinesEx();
        g_data.DebugLog(2, L"WheelUp line=%d ctrl=%d flag=%d: '%s' -> '%s'", m_line_index, ctrl_down ? 1 : 0, flag, before.line1.c_str(), after.line1.c_str());
        ::InvalidateRect((HWND)hWnd, nullptr, TRUE);
        return 1;
    }
    case IPluginItem::MT_WHEEL_DOWN:
    {
        auto before = g_data.GetTaskbarLinesEx();
        if (line2_roll_detail && ctrl_down)
            g_data.StepLine2Detail(1);
        else
            g_data.StepActiveSymbol(1);
        auto after = g_data.GetTaskbarLinesEx();
        g_data.DebugLog(2, L"WheelDown line=%d ctrl=%d flag=%d: '%s' -> '%s'", m_line_index, ctrl_down ? 1 : 0, flag, before.line1.c_str(), after.line1.c_str());
        ::InvalidateRect((HWND)hWnd, nullptr, TRUE);
        return 1;
    }
    case IPluginItem::MT_RCLICKED:
        g_data.DebugLog(2, L"RClick line=%d flag=%d", m_line_index, flag);
        // 返回 0 让主程序弹出默认菜单（包含插件命令）
        return 0;
    default:
        break;
    }
    return 0;
}
