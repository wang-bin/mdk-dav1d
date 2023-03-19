/*
 * Copyright (c) 2021-2023 WangBin <wbsecg1 at gmail.com>
 */

#if __has_include("dav1d/dav1d.h")
#include "dav1d/common.h"
#undef DAV1D_API // visibility is "default"
#define DAV1D_API
#include "dav1d/dav1d.h"
#include <cassert>
#include <string>
#include <iostream>
# if __has_include("cppcompat/cstdlib.hpp")
#   include "cppcompat/cstdlib.hpp"
# else
#   include <cstdlib>
# endif
#if defined(_WIN32)
# ifndef UNICODE
#   define UNICODE 1
# endif
# include <windows.h>
# ifdef WINAPI_FAMILY
#  include <winapifamily.h>
#  if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#    define DAV1D_WINRT 1
#  endif
# endif
# if (DAV1D_WINRT+0)
#   define dlopen(filename, flags) LoadPackagedLibrary(filename, 0)
# else
#   define dlopen(filename, flags) LoadLibrary(filename)
# endif
# define dlsym(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
# define dlclose(handle) FreeLibrary((HMODULE)handle)
#else
# include <dlfcn.h>
#endif
using namespace std;

using dav1d_free_callback_t = void(*)(const uint8_t *buf, void *cookie);
#define DAV1D_ARG0() (), (), ()
#define DAV1D_ARG1(P1) (P1), (P1 p1), (p1)
#define DAV1D_ARG2(P1, P2) (P1, P2), (P1 p1, P2 p2), (p1, p2)
#define DAV1D_ARG3(P1, P2, P3) (P1, P2, P3), (P1 p1, P2 p2, P3 p3), (p1, p2, p3)
#define DAV1D_ARG4(P1, P2, P3, P4) (P1, P2, P3, P4), (P1 p1, P2 p2, P3 p3, P4 p4), (p1, p2, p3, p4)
#define DAV1D_ARG5(P1, P2, P3, P4, P5) (P1, P2, P3, P4, P5), (P1 p1, P2 p2, P3 p3, P4 p4, P5 p5), (p1, p2, p3, p4, p5)


#define _DAV1D_API(R, NAME, ...) DAV1D_API_EXPAND(DAV1D_API_EXPAND_T_V(R, NAME, __VA_ARGS__))
#define DAV1D_API_EXPAND(EXPR) EXPR
#define DAV1D_API_EXPAND_T_V(R, F, ARG_T, ARG_T_V, ARG_V) \
    R F ARG_T_V { \
        static const auto fp = (decltype(&(F)))dlsym(load_once(), #F); \
        if (!fp) \
            return default_rv<R>(); \
        return fp ARG_V; \
    }

template<typename T> T default_rv() {return {};}
template<> void default_rv<void>() {}

inline string to_string(const wchar_t* ws)
{
    string s(snprintf(nullptr, 0, "%ls", ws), 0);
    snprintf(&s[0], s.size() + 1, "%ls", ws);
    return s;
}

inline string to_string(const char* s) { return s;}

static auto libname(int version = -1)
{
    if (version < 0) {
        return
#if (_WIN32+0)
        basic_string<TCHAR>(TEXT("libdav1d.dll"))
#elif (__APPLE__+0)
        string("libdav1d.dylib")
#else
        string("libdav1d.so")
#endif
        ;
    }
#if (_WIN32+0)
    return L"libdav1d-" + to_wstring(version) + L".dll";
#elif (__APPLE__+0)
    return "libdav1d." + to_string(version) + ".dylib";
#else
    return "libdav1d.so." + to_string(version);
#endif
}

static auto load_dav1d()->decltype(dlopen(nullptr, RTLD_LAZY))
{
    const auto dso_env_a = std::getenv("DAV1D_LIB");
#if (_WIN32+0)
    wchar_t dso_env_w[128+1]; // enough. strlen is not const expr
    if (dso_env_a)
        mbstowcs(dso_env_w, dso_env_a, strlen(dso_env_a)+1);
    const auto dso_env = dso_env_a ? dso_env_w : nullptr;
#else
    const auto dso_env = dso_env_a;
#endif
    if (dso_env)
        return dlopen(dso_env, RTLD_NOW | RTLD_LOCAL);
    invoke_result_t<decltype(libname), int> preName;
    for (auto v : {
        6, 5, -1
    }) {
        const auto name = libname(v);
        if (preName == name)
            continue;
        preName = name;
        clog << "Try to load dav1d runtime: " << to_string(name.data()) << endl;
        if (auto dso = dlopen(name.data(), RTLD_NOW | RTLD_LOCAL))
            return dso;
    }
    clog << "Failed to load dav1d runtime" << endl;
    return nullptr;
}

static auto load_once()
{
    static auto dso = load_dav1d();
    return dso;
}

extern "C" {
_DAV1D_API(const char*, dav1d_version, DAV1D_ARG0())
_DAV1D_API(void, dav1d_default_settings, DAV1D_ARG1(Dav1dSettings*))
_DAV1D_API(int, dav1d_open, DAV1D_ARG2(Dav1dContext**, const Dav1dSettings*))
_DAV1D_API(int, dav1d_parse_sequence_header, DAV1D_ARG3(Dav1dSequenceHeader*, const uint8_t*, const size_t))
_DAV1D_API(int, dav1d_send_data, DAV1D_ARG2(Dav1dContext*, Dav1dData*))
_DAV1D_API(int, dav1d_get_picture, DAV1D_ARG2(Dav1dContext*, Dav1dPicture*))
_DAV1D_API(void, dav1d_close, DAV1D_ARG1(Dav1dContext**))
_DAV1D_API(void, dav1d_flush, DAV1D_ARG1(Dav1dContext*))
_DAV1D_API(uint8_t*, dav1d_data_create, DAV1D_ARG2(Dav1dData*, size_t))
_DAV1D_API(int, dav1d_data_wrap, DAV1D_ARG5(Dav1dData*, const uint8_t*, size_t, dav1d_free_callback_t, void*))
_DAV1D_API(int, dav1d_data_wrap_user_data, DAV1D_ARG4(Dav1dData*, const uint8_t *, dav1d_free_callback_t, void*))
_DAV1D_API(void, dav1d_data_unref, DAV1D_ARG1(Dav1dData*))
_DAV1D_API(void, dav1d_picture_unref, DAV1D_ARG1(Dav1dPicture*))
}
#endif //__has_include("dav1d/dav1d.h")