#include "pch.h"

#include "Settings.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Security::Credentials;
using namespace Windows::Storage;

namespace
{
    // one locker entry per mode, keyed by the ID it signs in as
    hstring vault_resource(LoginMode m)
    {
        return m == LoginMode::PlayOnline ? L"XI on Xbox: PlayOnline" : L"XI on Xbox: direct";
    }

    std::string text(Windows::Foundation::Collections::IPropertySet const& values, wchar_t const* key)
    {
        return to_string(unbox_value_or<hstring>(values.TryLookup(key), L""));
    }

    void forget_password(LoginMode m)
    {
        try
        {
            PasswordVault vault;
            for (auto const& c : vault.FindAllByResource(vault_resource(m)))
                vault.Remove(c);
        }
        catch (hresult_error const&) // none stored
        {
        }
    }
}

SavedLogin LoadLogin()
{
    SavedLogin s;
    auto values = ApplicationData::Current().LocalSettings().Values();
    s.details.mode = unbox_value_or<int32_t>(values.TryLookup(L"mode"), 0) ? LoginMode::Direct : LoginMode::PlayOnline;
    s.details.server = text(values, L"server");
    s.details.id = text(values, s.details.mode == LoginMode::PlayOnline ? L"pol_id" : L"account");
    s.remember_password = unbox_value_or<bool>(values.TryLookup(L"remember"), false);
    if (s.remember_password && !s.details.id.empty())
    {
        try
        {
            PasswordCredential c = PasswordVault().Retrieve(vault_resource(s.details.mode), to_hstring(s.details.id));
            c.RetrievePassword();
            s.details.password = to_string(c.Password());
        }
        catch (hresult_error const&)
        {
        }
    }
    return s;
}

void SaveLogin(LoginDetails const& d, bool remember_password)
{
    auto values = ApplicationData::Current().LocalSettings().Values();
    values.Insert(L"mode", box_value(int32_t(d.mode == LoginMode::Direct)));
    values.Insert(L"server", box_value(to_hstring(d.server)));
    values.Insert(d.mode == LoginMode::PlayOnline ? L"pol_id" : L"account", box_value(to_hstring(d.id)));
    values.Insert(L"remember", box_value(remember_password));
    forget_password(d.mode);
    if (remember_password && !d.id.empty() && !d.password.empty())
        PasswordVault().Add(PasswordCredential(vault_resource(d.mode), to_hstring(d.id), to_hstring(d.password)));
}

// the ID field shows the one saved for the mode switched to
std::string SavedId(LoginMode m)
{
    return text(ApplicationData::Current().LocalSettings().Values(), m == LoginMode::PlayOnline ? L"pol_id" : L"account");
}
