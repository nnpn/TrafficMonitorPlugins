#pragma once
#include "PluginInterface.h"
#include "PluginTemplateItem.h"
#include <string>

class CPluginTemplate : public ITMPlugin
{
private:
    CPluginTemplate();

public:
    static CPluginTemplate& Instance();

    virtual IPluginItem* GetItem(int index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual int GetCommandCount() override;
    virtual const wchar_t* GetCommandName(int command_index) override;
    virtual void OnPluginCommand(int command_index, void* hWnd, void* para) override;

private:
    static UINT ThreadCallback(LPVOID);
    void SendQuoteRequest();

private:
    static CPluginTemplate m_instance;
    CPluginTemplateItem m_item;
    std::wstring m_tooltip_info;

    bool m_is_thread_runing{};
    unsigned __int64 m_last_request_time{};
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
