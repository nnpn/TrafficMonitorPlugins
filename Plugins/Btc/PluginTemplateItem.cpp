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
    m_name_cache = g_data.StringRes(IDS_PLUGIN_ITEM_NAME).GetString();
    if (m_line_index == 0)
        m_name_cache += L" (1)";
    else
        m_name_cache += L" (2)";
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
    std::wstring sample = g_data.GetSampleText();
    int w1 = pDC->GetTextExtent(lines.first.c_str()).cx;
    int w2 = pDC->GetTextExtent(lines.second.c_str()).cx;
    int ws = pDC->GetTextExtent(sample.c_str()).cx;

    int space_w = pDC->GetTextExtent(L" ").cx;
    return std::max(ws, std::max(w1, w2)) + space_w;
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

    // 视觉间距：与前一列保持约一个空格宽度（右对齐模式下不强制）
    if (!right_align)
    {
        int space_w = pDC->GetTextExtent(L" ").cx;
        rect.left += space_w;
    }

    pDC->DrawText(text.c_str(), rect, flags | DT_VCENTER);
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
