#pragma once
#include "PluginInterface.h"

class CPluginTemplateItem : public IPluginItem
{
public:
    void SetLineIndex(int line_index);

    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override;
    virtual int GetItemWidthEx(void* hDC) const override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    virtual int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;

private:
    int m_line_index{};
    mutable std::wstring m_name_cache;
    mutable std::wstring m_id_cache;
    mutable std::wstring m_sample_cache;
};
