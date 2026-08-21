// SPDX-License-Identifier: BSD-3-Clause
//
// voice_token.hpp - an in-memory SAPI5 voice token.
//
// SAPI normally reads a voice's attributes out of the registry. Serving them
// from memory instead means the eight DoubleTalk voices need no registry
// entries of their own: the only key this project writes for discovery is the
// single TokenEnums entry naming the enumerator CLSID, and SAPI then asks this
// object for the voice list.

#pragma once

#include <map>
#include <string>

#include "ISpDataKeyImpl.hpp"
#include "dt_voices.h"
#include "utils.hpp"

namespace DoubleTalk {
namespace sapi {

class voice_token : public ISpDataKeyImpl
{
public:
    explicit voice_token(int voice_index);

    STDMETHOD(OpenKey)(LPCWSTR pszSubKeyName, ISpDataKey** ppSubKey) override;
    STDMETHOD(EnumKeys)(ULONG Index, LPWSTR* ppszSubKeyName) override;

private:
    [[nodiscard]] bool str_equal(const std::wstring& a, const std::wstring& b) const noexcept
    {
        return _wcsicmp(a.c_str(), b.c_str()) == 0;
    }

    using attribute_map = std::map<std::wstring, std::wstring, str_less>;
    attribute_map attributes_;
};

}
}
