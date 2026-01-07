#include "pch.h"
#include "PluginTemplateItem.h"
#include "DataManager.h"
#include <algorithm>
#undef min
#undef max

const wchar_t* CPluginTemplateItem::GetItemName() const
{
    return g_data.StringRes(IDS_PLUGIN_ITEM_NAME);
}

const wchar_t* CPluginTemplateItem::GetItemId() const
{
    return L"BtcPrice01";
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
    static std::wstring sample;
    sample = g_data.GetSampleText();
    return sample.c_str();
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
    return std::max(ws, std::max(w1, w2));
}

void CPluginTemplateItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    CDC* pDC = CDC::FromHandle((HDC)hDC);
    CRect rect(CPoint(x, y), CSize(w, h));

    const auto lines = g_data.GetTaskbarLines();
    const bool right_align = g_data.IsRightAlign();

    // 基础文本颜色（Phase 3 再按涨跌着色）
    COLORREF text_color = dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(text_color);

    TEXTMETRIC tm{};
    pDC->GetTextMetrics(&tm);
    int line_height = tm.tmHeight + tm.tmExternalLeading;
    if (line_height <= 0)
        line_height = 12;

    int max_lines = h / line_height;
    UINT flags = DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    if (right_align)
        flags |= DT_RIGHT;

    if (max_lines >= 2)
    {
        int total_h = line_height * 2;
        int top = rect.top + (rect.Height() - total_h) / 2;

        CRect r1 = rect;
        r1.top = top;
        r1.bottom = top + line_height;
        pDC->DrawText(lines.first.c_str(), r1, flags | DT_VCENTER);

        CRect r2 = rect;
        r2.top = top + line_height;
        r2.bottom = r2.top + line_height;
        pDC->DrawText(lines.second.c_str(), r2, flags | DT_VCENTER);
    }
    else
    {
        pDC->DrawText(lines.first.c_str(), rect, flags | DT_VCENTER);
    }
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
