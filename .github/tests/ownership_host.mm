#import <Carbon/Carbon.h>
#include "4DPluginAPI.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"
#include <codecvt>
#include <cstring>
#include <iostream>
#include <locale>

static std::map<PA_Unichar*, size_t> strings;
static std::map<PA_Picture, int> pictures;
static std::map<PA_Handle, size_t> handles;
static bool nullEmpty = false;
static bool failHandle = false;
static CUTF16String expected;

static void check(bool valid, const char* message)
{
    if (!valid) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

static PA_Unistring makeString(const PA_Unichar* text)
{
    size_t units = 0;
    while (text && text[units]) ++units;
    PA_Unistring value = {};
    if (units || !nullEmpty) {
        value.fString = new PA_Unichar[units + 1];
        if (units) std::memcpy(value.fString, text, units * sizeof(PA_Unichar));
        value.fString[units] = 0;
        value.fLength = static_cast<PA_long32>(units);
        strings[value.fString] = units;
    }
    return value;
}

static void disposeString(PA_Unistring value)
{
    if (!value.fString) {
        check(value.fLength == 0, "null string with nonzero length");
        return;
    }
    check(strings.count(value.fString), "duplicate/unowned string disposal");
    check(strings.at(value.fString) == static_cast<size_t>(value.fLength),
          "string length does not match allocation");
    strings.erase(value.fString);
    delete[] value.fString;
}

static void textEquals(PA_Unistring value, const CUTF16String& wanted)
{
    check(value.fLength == static_cast<PA_long32>(wanted.size()), "exact UTF16 length");
    check(value.fString || wanted.empty(), "missing text allocation");
    if (value.fString) {
        check(strings.count(value.fString), "read after string release");
        check(value.fString[value.fLength] == 0, "missing text terminator");
        check(CUTF16String(value.fString, value.fLength) == wanted, "exact UTF16 content");
    }
}

static PA_Picture makePicture()
{
    PA_Picture p = new unsigned char(42);
    pictures[p] = 1;
    return p;
}

static void releasePicture(PA_Picture p)
{
    if (!p) return;
    check(pictures.count(p), "duplicate/unowned picture disposal");
    if (--pictures[p] == 0) {
        pictures.erase(p);
        delete static_cast<unsigned char*>(p);
    }
}

static void clear(PA_Variable* v)
{
    if (v->fType == eVK_Unistring) disposeString(v->uValue.fString);
    else if (v->fType == eVK_Picture) releasePicture(v->uValue.fPicture);
    else if (v->fType == eVK_ArrayUnicode) {
        auto h = v->uValue.fArray.fData;
        check(handles.count(h), "invalid array handle");
        auto values = reinterpret_cast<PA_Unistring*>(*h);
        for (PA_long32 i = 0; i <= v->uValue.fArray.fNbElements; ++i)
            disposeString(values[i]);
        std::free(*h);
        delete h;
        handles.erase(h);
    } else check(v->fType == eVK_Undefined || v->fType == eVK_Longint,
                 "unsupported clear kind (including raw-kind0 failure sentinel)");
    v->fType = eVK_Undefined;
    v->fFiller = 0;
}

static void host(short selector, EngineBlock* eb)
{
    switch (selector) {
    case EX_CREATE_UNISTRING:
        eb->fUniString1 = makeString(reinterpret_cast<PA_Unichar*>(eb->fHandle));
        break;
    case EX_DISPOSE_UNISTRING:
        disposeString(eb->fUniString1);
        break;
    case EX_SET_UNISTRING: {
        auto replacement = makeString(reinterpret_cast<PA_Unichar*>(eb->fHandle));
        disposeString(eb->fUniString1);
        eb->fUniString1 = replacement;
        break;
    }
    case EX_CREATE_PICTURE:
        check(eb->fParam1 == 0 || eb->fPtr1, "picture bytes missing");
        eb->fPicture = makePicture();
        eb->fError = 0;
        break;
    case EX_DISPOSE_PICTURE:
        releasePicture(eb->fPicture);
        break;
    case EX_DUPLICATE_PICTURE:
        check(pictures.count(eb->fPicture), "retain dead picture");
        if (eb->fParam1 == 0) ++pictures[eb->fPicture];
        else eb->fPicture = makePicture();
        break;
    case EX_GET_PICTURE_DATA:
        check(pictures.count(eb->fPicture), "enumerate dead picture");
        eb->fError = eER_IncompatibleTypes;
        // Failure outputs must never be inspected or adopted by callers.
        eb->fUniString1.fLength = -1;
        eb->fUniString1.fString = reinterpret_cast<PA_Unichar*>(1);
        break;
    case EX_CLEAR_VARIABLE:
        clear(static_cast<PA_Variable*>(eb->fPtr1));
        break;
    case EX_HANDLE_MANAGER: {
        auto h = eb->fHandle;
        switch (eb->fParam1) {
        case 1:
            if (failHandle) {
                eb->fHandle = nullptr;
                eb->fError = eER_IncompatibleTypes;
                break;
            }
            h = new char*;
            *h = static_cast<char*>(std::calloc(1, std::max<sLONG_PTR>(1, eb->fParam2)));
            check(*h, "test handle allocation failed");
            handles[h] = eb->fParam2;
            eb->fHandle = h;
            break;
        case 2:
            check(handles.count(h), "dispose dead handle");
            std::free(*h);
            delete h;
            handles.erase(h);
            break;
        case 3: {
            check(handles.count(h), "resize dead handle");
            auto data = static_cast<char*>(std::realloc(*h, std::max<sLONG_PTR>(1, eb->fParam2)));
            check(data, "test resize allocation failed");
            *h = data;
            handles[h] = eb->fParam2;
            break;
        }
        case 4:
            check(handles.count(h), "size dead handle");
            eb->fParam2 = handles[h];
            break;
        case 5:
            check(handles.count(h), "lock dead handle");
            eb->fParam3 = reinterpret_cast<sLONG_PTR>(*h);
            break;
        case 6:
            check(handles.count(h), "unlock dead handle");
            break;
        default: check(false, "unexpected handle operation");
        }
        break;
    }
    case EX_CONVERT_CHARSET_TO_CHARSET: {
        check(eb->fParam2 == eVTC_UTF_8 && eb->fParam4 == eVTC_UTF_16,
              "unexpected conversion");
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> codec;
        auto in = static_cast<char*>(eb->fPtr1);
        auto out = codec.from_bytes(in, in + eb->fParam1);
        size_t bytes = out.size() * sizeof(PA_Unichar);
        check(bytes + sizeof(PA_Unichar) <= static_cast<size_t>(eb->fParam3),
              "UTF16 capacity excludes terminator");
        std::memcpy(eb->fPtr2, out.data(), bytes);
        std::memset(static_cast<char*>(eb->fPtr2) + bytes, 0, sizeof(PA_Unichar));
        eb->fParam3 = bytes;
        eb->fError = 0;
        break;
    }
    case EX_EXECUTE_COMMAND_BY_ID: {
        auto args = static_cast<PA_Variable**>(eb->fPtr1);
        auto result = static_cast<PA_Variable*>(eb->fPtr2);
        check(result->fType == eVK_Undefined, "command return not initialized");
        if (eb->fParam1 == 678) {
            textEquals(args[0]->uValue.fString, expected);
            clear(args[1]);
            PA_SetPictureVariable(args[1], makePicture());
        } else if (eb->fParam1 == 1002) {
            textEquals(args[1]->uValue.fString, expected);
            clear(args[0]);
            PA_SetPictureVariable(args[0], makePicture());
        } else if (eb->fParam1 == 988) {
            clear(args[0]);
            PA_SetPictureVariable(args[0], makePicture());
        } else if (eb->fParam1 == 457) {
            check(pictures.count(args[0]->uValue.fPicture), "borrowed size input dead");
            PA_SetLongintVariable(args[1], 23);
            PA_SetLongintVariable(args[2], 17);
        } else if (eb->fParam1 == 679) {
            check(pictures.count(args[0]->uValue.fPicture), "borrowed thumbnail input dead");
            clear(args[1]);
            PA_SetPictureVariable(args[1], makePicture());
        } else check(false, "unexpected command");
        eb->fError = 0;
        break;
    }
    default:
        std::cerr << "unexpected engine selector " << selector << "\n";
        std::exit(1);
    }
}

void PluginMain(PA_long32, PA_PluginParameters) {}

static CUTF16String utf16(const char* text)
{
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> codec;
    auto converted = codec.from_bytes(text);
    return CUTF16String(reinterpret_cast<const PA_Unichar*>(converted.data()), converted.size());
}

static void pictureCase(const std::string& name, const char* text)
{
    expected = utf16(text);
    PA_Picture handoff = nullptr;
    PA_Picture returned = nullptr;
    PA_Picture param = makePicture();
    {
        CPicture picture;
        if (name == "path") {
            CUTF8String path(reinterpret_cast<const uint8_t*>(text));
            picture.fromFileAtPath(&path);
        } else if (name == "bytes") {
            const uint8_t synthetic[] = {1, 2, 3};
            picture.setBytes(synthetic, sizeof(synthetic));
            picture.setBytes(synthetic, sizeof(synthetic));
        } else if (name == "copy") {
            handoff = picture.createCopy(text);
            check(pictures.count(handoff) && pictures[handoff] == (failHandle ? 2 : 1),
                  "copy return is not sole owned reference");
        } else if (name == "gray") {
            handoff = picture.createGrayScale();
            check(pictures.count(handoff) && pictures[handoff] == (failHandle ? 2 : 1),
                  "gray return is not sole owned reference");
        } else if (name == "size") {
            unsigned width = 0, height = 0;
            picture.getSize(&width, &height);
            check(width == (failHandle ? 0u : 23u) && height == (failHandle ? 0u : 17u),
                  "size outputs");
        } else if (name == "thumbnail") {
            handoff = picture.createThumbnail(23, 17);
            check(pictures.count(handoff) && pictures[handoff] == 1, "thumbnail return ownership");
        } else check(false, "unknown picture test");
        check(pictures.count(picture.getPicture()), "wrapper picture died during callback");
        int baseRefs = handoff == picture.getPicture() ? 2 : 1;
        picture.setReturn(reinterpret_cast<sLONG_PTR*>(&returned));
        check(pictures[returned] == baseRefs + 1, "return must independently retain");
        BytePtr params[] = {reinterpret_cast<BytePtr>(&param)};
        picture.toParamAtIndex(params, 1);
        check(param == returned && pictures[param] == baseRefs + 2,
              "parameter must independently retain");
    }
    check(pictures.count(returned) && pictures[returned] == (handoff == returned ? 3 : 2),
          "return/parameter must survive wrapper destruction");
    PA_DisposePicture(returned);
    PA_DisposePicture(param);
    if (handoff) {
        check(pictures.count(handoff) && pictures[handoff] == 1, "owning handoff did not survive");
        PA_DisposePicture(handoff);
    }
}

static void scalarCase(const char* text)
{
    auto wanted = utf16(text);
    auto input = PA_CreateUnistring(const_cast<PA_Unichar*>(wanted.c_str()));
    auto result = PA_CreateUnistring(nullptr);
    auto output = PA_CreateUnistring(nullptr);
    {
        C_TEXT wrapper;
        BytePtr params[] = {reinterpret_cast<BytePtr>(&input)};
        wrapper.fromParamAtIndex(params, 1);
        auto changed = utf16("changed");
        PA_SetUnistring(&input, const_cast<PA_Unichar*>(changed.c_str()));
        params[0] = reinterpret_cast<BytePtr>(&output);
        wrapper.toParamAtIndex(params, 1);
        auto resultPtr = &result;
        wrapper.setReturn(reinterpret_cast<sLONG_PTR*>(&resultPtr));
    }
    textEquals(result, wanted);
    textEquals(output, wanted);
    PA_DisposeUnistring(&input);
    PA_DisposeUnistring(&output);
    PA_DisposeUnistring(&result);

    auto owned = PA_CreateUnistring(const_cast<PA_Unichar*>(wanted.c_str()));
    result = PA_CreateUnistring(nullptr);
    auto resultPtr = &result;
    PluginBlock block = {};
    block.fResult = &resultPtr;
    PA_ReturnString(&block, owned.fString);
    PA_DisposeUnistring(&owned);
    textEquals(result, wanted);
    PA_DisposeUnistring(&result);
}

static void arrayCase()
{
    auto arr = PA_CreateVariable(eVK_ArrayUnicode);
    PA_ResizeArray(&arr, 4);
    const char* fixtures[] = {"ASCII", "\xe6\x97\xa5", "\xf0\x9f\x98\x80", "", ""};
    for (int i = 0; i <= 4; ++i) {
        auto text = utf16(fixtures[i]);
        auto owned = PA_CreateUnistring(const_cast<PA_Unichar*>(text.c_str()));
        PA_SetStringInArray(arr, i, &owned);
    }
    BytePtr params[] = {reinterpret_cast<BytePtr>(&arr)};
    ARRAY_TEXT copy;
    copy.fromParamAtIndex(params, 1);
    auto before = PA_GetStringInArray(arr, 0);
    auto replacementText = utf16("replacement");
    auto replacement = PA_CreateUnistring(const_cast<PA_Unichar*>(replacementText.c_str()));
    check(before.fString != replacement.fString, "replacement must allocate independently");
    PA_SetStringInArray(arr, 0, &replacement);
    copy.toParamAtIndex(params, 1);
    for (int i = 0; i <= 4; ++i) textEquals(PA_GetStringInArray(arr, i), utf16(fixtures[i]));
    check(PA_GetStringInArray(arr, 0).fString != PA_GetStringInArray(arr, 1).fString,
          "separate outputs share allocation");
    auto reject = PA_CreateUnistring(const_cast<PA_Unichar*>(replacementText.c_str()));
    PA_SetStringInArray(arr, -1, &reject);
    PA_SetStringInArray(arr, 5, &reject);
    auto wrong = PA_CreateVariable(eVK_Longint);
    PA_SetStringInArray(wrong, 0, &reject);
    auto missing = arr;
    missing.uValue.fArray.fData = nullptr;
    PA_SetStringInArray(missing, 0, &reject);
    PA_SetStringInArray(arr, 0, nullptr);
    textEquals(reject, replacementText);
    PA_DisposeUnistring(&reject);
    copy.setSize(2);
    copy.toParamAtIndex(params, 1);
    copy.setSize(0);
    copy.toParamAtIndex(params, 1);
    PA_ClearVariable(&arr);

    auto inputText = utf16("callback");
    auto input = PA_CreateUnistring(const_cast<PA_Unichar*>(inputText.c_str()));
    auto arg = PA_CreateVariable(eVK_Undefined);
    PA_SetStringVariable(&arg, &input);
    // This allocation is independent from every transferred array element.
    textEquals(arg.uValue.fString, inputText);
    PA_ClearVariable(&arg);

    failHandle = true;
    auto failure = PA_ExecuteCommandByID(457, nullptr, 0);
    check(failure.fType == 0, "command allocation failure must retain raw-kind0 sentinel");
    check(PA_GetVariableKind(failure) == eVK_Unistring,
          "SDK normalization no longer misclassifies raw-kind0 sentinel");
    auto methodFailure = PA_ExecuteMethodByID(1, nullptr, 0);
    check(methodFailure.fType == eVK_Undefined, "method failure kind");
    failHandle = false;
    // Do not normalize/clear the command's raw-kind0 failure sentinel.
}

int main(int argc, char** argv)
{
    check(argc >= 2, "test name required");
    gCall4D = host;
    nullEmpty = argc >= 4 && std::string(argv[3]) == "null";
    std::string name = argv[1];
    const std::string suffix = "-handle-failure";
    if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(),
                                                     suffix.size(), suffix) == 0) {
        name.resize(name.size() - suffix.size());
        failHandle = true;
    }
    if (name == "arrays") arrayCase();
    else if (name == "scalar") scalarCase(argc >= 3 ? argv[2] : "");
    else pictureCase(name, argc >= 3 ? argv[2] : "");
    check(strings.empty(), "unbalanced string allocations");
    check(pictures.empty(), "unbalanced picture references");
    check(handles.empty(), "unbalanced handles");
    std::cout << "PASS " << name << " strings=0 pictures=0 handles=0\n";
}
