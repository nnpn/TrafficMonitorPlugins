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

    const auto lines = g_data.GetTaskbarLines();
    const bool right_align = g_data.IsRightAlign();
    const std::wstring& text = (m_line_index == 0 ? lines.first : lines.second);

    // 基础文本颜色（Phase 3 再按涨跌着色）
    COLORREF text_color = dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(text_color);

    UINT flags = DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    if (right_align)
        flags |= DT_RIGHT;

    // 视觉间距：统一使用一个前导空格，避免过近，同时不引入额外尾部留白
    std::wstring draw = L" ";
    draw += text;
    pDC->DrawText(draw.c_str(), rect, flags | DT_VCENTER);
}

int CPluginTemplateItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
{
    switch (type)
    {
    case IPluginItem::MT_WHEEL_UP:
        g_data.StepActiveSymbol(-1);
        return 1;
    case IPluginItem::MT_WHEEL_DOWN:
        g_data.StepActiveSymbol(1);
        return 1;
    default:
        break;
    }
    return 0;
}
