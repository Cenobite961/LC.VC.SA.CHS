#include "FontPatch.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <plugin.h>
#include <injector.hpp>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

namespace FontPatch
{
    typedef wchar_t CharType;

    CharType* (__cdecl* fnGInput_ParseToken)(CharType*, CRGBA&, bool&, bool&);
    CharType* (__stdcall* fnGInput_SkipToken)(CharType*, float*);
    float(__cdecl* fnGInput_PrintSymbol)(float, float);
    char* GInput_ButtonSymbol;

    typedef CharType* (__cdecl* ParseTokenFn)(CharType*, CRGBA&, bool&, bool&);
    ParseTokenFn g_GInputParseToken = nullptr;

    static bool g_GInputAvailable = false;

    static void(__cdecl* g_fpDrawBackground)(CRect*, void*) = nullptr;
    static void* g_pBackgroundParam = nullptr;

    class CFontSizes {
    public:
        short PropValues[192];
        short UnpropValue;
    };

    static int* RsGlobalW = nullptr;
    static int* RsGlobalH = nullptr;
    static CFontSizes* g_Size = nullptr;
    static CFontDetails* g_Details = nullptr;
    static void* g_fpPrintChar = nullptr;
    static void* g_fpParseToken = nullptr;

    static char datPath[MAX_PATH];
    static char textPath[MAX_PATH];
    static char texturePath[MAX_PATH];

    static CSprite2d g_ChsSprite;
    static CSprite2d g_ChsSlantSprite;

    constexpr int TABLE_SIZE = 0x10000;
    struct CharPos {
        unsigned char rowIndex;
        unsigned char columnIndex;
    };
    static std::array<CharPos, TABLE_SIZE> sTable;

    constexpr float GRID_ROWS = 64.0f;
    constexpr float GRID_COLS = 64.0f;
    constexpr float ROW_STEP = 1.0f / GRID_ROWS;
    constexpr float COL_STEP = 1.0f / GRID_COLS;
    constexpr float TEX_UFIX = 0.001f / 4.0f;
    constexpr float TEX_VFIX = 0.001f / 4.0f;

    constexpr float CHS_CHAR_HEIGHT = 18.0f;
    constexpr float CHS_CHAR_WIDTH = 28.0f;
    constexpr float BUTTON_WIDTH = 32.0f;

    void ReadTable()
    {
        sTable.fill({ 63, 63 });
        FILE* hfile = std::fopen(datPath, "rb");
        if (hfile != nullptr)
        {
            std::fseek(hfile, 0, SEEK_END);
            if (std::ftell(hfile) == TABLE_SIZE * sizeof(CharPos))
            {
                std::fseek(hfile, 0, SEEK_SET);
                std::fread(sTable.data(), 2, TABLE_SIZE, hfile);
            }
            std::fclose(hfile);
        }
    }

    CharPos GetCharPos(CharType chr) {
        if (chr < 0x60) {
            return { (unsigned char)(chr >> 4), (unsigned char)(chr & 0xF) };
        }
        return sTable[chr];
    }

    float GetCharacterSize(CharType ch) {
        if (ch >= 0x80) {
            return CHS_CHAR_WIDTH * g_Details->m_vScale.x;
        }
        short w;
        if (g_Details->m_bProp)
            w = g_Size[g_Details->m_nStyle].PropValues[ch - 0x20];
        else
            w = g_Size[g_Details->m_nStyle].UnpropValue;
        return w * g_Details->m_vScale.x;
    }

    bool HasValidToken(const CharType* text) {
        if (*text != L'~') return false;
        const CharType* p = text + 1;
        while (*p && *p != L'~') ++p;
        return (*p == L'~');
    }

    CharType* ParseTokenManual(CharType* text, bool* isButton, float* outWidth)
    {
        *isButton = false;
        *outWidth = 0.0f;
        if (*text != L'~') return text + 1;

        CharType* p = text + 1;
        while (*p && *p != L'~') ++p;
        if (*p != L'~') return p;

        int innerLen = p - text - 1;
        if (innerLen == 1) {
            char tokenChar = static_cast<char>(*(text + 1));
            if (g_GInputAvailable && GInput_ButtonSymbol &&
                strchr(GInput_ButtonSymbol, tokenChar) != nullptr)
            {
                *isButton = true;
                *outWidth = BUTTON_WIDTH * g_Details->m_vScale.x;
            }
        }
        return p + 1;
    }

    float GetStringWidth(CharType* text, bool all) {
        float res = 0.0f;
        while (*text) {
            if (*text == ' ') {
                if (all) res += GetCharacterSize(' ');
                else break;
            }
            else if (*text == '~') {
                CharType* p = text + 1;
                while (*p && *p != L'~') ++p;
                if (*p == L'~') {
                    int innerLen = p - text - 1;
                    if (innerLen == 1) {
                        CharType tokenChar = *(text + 1);
                        bool isColor = (tokenChar == L'b' || tokenChar == L'g' || tokenChar == L'h' ||
                            tokenChar == L'l' || tokenChar == L'p' || tokenChar == L'r' ||
                            tokenChar == L'w' || tokenChar == L'y');
                        bool isNewline = (tokenChar == L'n' || tokenChar == L'N');
                        if (!isColor && !isNewline) {
                            res += BUTTON_WIDTH * g_Details->m_vScale.x;
                        }
                    }
                    text = p + 1;
                }
                else {
                    text = p;
                }
                continue;
            }
            else if (*text < 0x80) {
                res += GetCharacterSize(*text);
            }
            else {
                if (res == 0.0f || all) res += GetCharacterSize(*text);
                if (!all) break;
            }
            ++text;
        }
        return res;
    }

    CharType* GetNextSpace(CharType* text) {
        CharType* p = text;
        bool succeeded = false;
        while (*p != ' ' && *p) {
            if (*p == '~') {
                if (p == text) {
                    CharType* scan = p + 1;
                    while (*scan && *scan != L'~') ++scan;
                    if (*scan == L'~') p = scan + 1;
                    else p = scan;
                    text = p;
                    continue;
                }
                else {
                    break;
                }
            }
            else if (*p < 0x80) {
                succeeded = true;
            }
            else {
                if (p == text || !succeeded) {
                    ++p;
                }
                break;
            }
            ++p;
        }
        return p;
    }

    short GetNumberLines(float x, float y, CharType* text) {
        short lines = 0;
        float curX = (g_Details->m_bCentre || g_Details->m_bRightJustify) ? 0.0f : x;
        float curY = y;
        while (*text) {
            if (text[0] == L'~' && (text[1] == L'n' || text[1] == L'N') && text[2] == L'~')
            {
                ++lines;
                curX = (g_Details->m_bCentre || g_Details->m_bRightJustify) ? 0.0f : x;
                curY += g_Details->m_vScale.y * CHS_CHAR_HEIGHT;
                text += 3;
                continue;
            }
            float width = GetStringWidth(text, false);
            float limit = g_Details->m_bCentre ? g_Details->m_fCentreSize : g_Details->m_fWrapX;
            if ((curX + width) <= limit) {
                curX += width;
                text = GetNextSpace(text);
                if (*text == ' ') {
                    curX += GetCharacterSize(' ');
                    ++text;
                }
                else if (*text == 0) {
                    ++lines;
                }
            }
            else {
                curX = (g_Details->m_bCentre || g_Details->m_bRightJustify) ? 0.0f : x;
                ++lines;
                curY += g_Details->m_vScale.y * CHS_CHAR_HEIGHT;
            }
        }
        return lines;
    }

    void GetTextRect(CRect* rect, float x, float y, CharType* text) {
        short lines = GetNumberLines(x, y, text);
        if (g_Details->m_bCentre) {
            if (g_Details->m_bBackGroundOnlyText) {
                rect->left = x - 4.0f;
                rect->right = x + 4.0f;
                rect->bottom = (CHS_CHAR_HEIGHT * g_Details->m_vScale.y) * lines + y + 2.0f;
                rect->top = y - 2.0f;
            }
            else {
                rect->left = x - g_Details->m_fCentreSize * 0.5f - 4.0f;
                rect->right = x + g_Details->m_fCentreSize * 0.5f + 4.0f;
                rect->bottom = y + (CHS_CHAR_HEIGHT * g_Details->m_vScale.y * lines) + 2.0f;
                rect->top = y - 2.0f;
            }
        }
        else {
            rect->left = x - 4.0f;
            rect->right = g_Details->m_fWrapX;
            rect->bottom = y;
            rect->top = (CHS_CHAR_HEIGHT * g_Details->m_vScale.y) * lines + y + 4.0f;
        }
    }

    void PrintCHSChar(float x, float y, CharType ch) {
        if (x >= *RsGlobalW || x <= 0.0f || y <= 0.0f || y >= *RsGlobalH) return;

        CharPos pos = GetCharPos(ch);
        float u1 = pos.columnIndex * COL_STEP;
        float v1 = pos.rowIndex * ROW_STEP;
        float u2 = (pos.columnIndex + 1) * COL_STEP - TEX_UFIX;
        float v2 = v1;
        float u3 = u1;
        float v3 = (pos.rowIndex + 1) * ROW_STEP - TEX_VFIX;
        float u4 = u2;
        float v4 = v3;

        CRect rect;
        rect.left = x;
        rect.bottom = y + g_Details->m_vScale.y * 20.0f;
        rect.right = x + g_Details->m_vScale.x * 32.0f;
        rect.top = y;

        RwD3D8Vertex verts[6];
        CSprite2d::SetVertices(verts, rect, g_Details->m_Color, g_Details->m_Color,
            g_Details->m_Color, g_Details->m_Color,
            u1, v1, u2, v2, u3, v3, u4, v4);

        RwTexture* tex = (g_Details->m_nStyle == 0) ?
            g_ChsSlantSprite.m_pTexture : g_ChsSprite.m_pTexture;
        RwRaster* raster = *(RwRaster**)((char*)tex + 0);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);
        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)2);
        RwIm2DRenderPrimitive(rwPRIMTYPETRILIST, verts, 6);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, nullptr);
    }

    void PrintCharDispatcher(float x, float y, CharType ch) {
        if (ch < 0x80) {
            ((void(__cdecl*)(float, float, CharType))g_fpPrintChar)(x, y, ch - 0x20);
        }
        else {
            PrintCHSChar(x, y, ch);
        }
    }

    void ApplyColorCode(CharType tokenChar)
    {
        switch (tokenChar)
        {
        case L'b':
            g_Details->m_Color.r = 128;
            g_Details->m_Color.g = 167;
            g_Details->m_Color.b = 243;
            break;
        case L'g':
            g_Details->m_Color.r = 95;
            g_Details->m_Color.g = 160;
            g_Details->m_Color.b = 106;
            break;
        case L'h':
            g_Details->m_Color.r = 225;
            g_Details->m_Color.g = 225;
            g_Details->m_Color.b = 225;
            break;
        case L'l':
            g_Details->m_Color.r = 0;
            g_Details->m_Color.g = 0;
            g_Details->m_Color.b = 0;
            break;
        case L'p':
            g_Details->m_Color.r = 168;
            g_Details->m_Color.g = 110;
            g_Details->m_Color.b = 252;
            break;
        case L'r':
            g_Details->m_Color.r = 113;
            g_Details->m_Color.g = 43;
            g_Details->m_Color.b = 73;
            break;
        case L'w':
            g_Details->m_Color.r = 175;
            g_Details->m_Color.g = 175;
            g_Details->m_Color.b = 175;
            break;
        case L'y':
            g_Details->m_Color.r = 210;
            g_Details->m_Color.g = 196;
            g_Details->m_Color.b = 106;
            break;
        default:
            break;
        }
    }

    void __cdecl PrintStringHook(float x, float y, CharType* text)
    {
        if (*text == L'*') return;

        if (g_Details->m_bBackground) {
            short lines = GetNumberLines(x, y, text);
            CRect rect;
            GetTextRect(&rect, x, y, text);
            if (g_fpDrawBackground && g_pBackgroundParam) {
                g_fpDrawBackground(&rect, g_pBackgroundParam);
            }
        }

        if (g_Details->m_bCentre) {
            float totalWidth = GetStringWidth(text, true);
            x -= totalWidth * 0.5f;
        }
        else if (g_Details->m_bRightJustify) {
            float totalWidth = GetStringWidth(text, true);
            x -= totalWidth;
        }

        float lineStartX = x;
        float lineStartY = y;

        while (*text)
        {
            if (*text == L' ')
            {
                x += GetCharacterSize(L' ');
                ++text;
            }
            else if (*text == L'~')
            {
                CharType* p = text + 1;
                while (*p && *p != L'~') ++p;
                if (*p != L'~') { text = p; break; }

                int innerLen = p - text - 1;
                if (innerLen == 1)
                {
                    CharType tokenChar = *(text + 1);

                    if (tokenChar == L'b' || tokenChar == L'g' || tokenChar == L'h' ||
                        tokenChar == L'l' || tokenChar == L'p' || tokenChar == L'r' ||
                        tokenChar == L'w' || tokenChar == L'y')
                    {
                        ApplyColorCode(tokenChar);
                        text = p + 1;
                        continue;
                    }
                    if (tokenChar == L'n' || tokenChar == L'N')
                    {
                        x = lineStartX;
                        y += CHS_CHAR_HEIGHT * g_Details->m_vScale.y;
                        text = p + 1;
                        continue;
                    }

                    float btnWidth = BUTTON_WIDTH * g_Details->m_vScale.x;
                    if (x + btnWidth > g_Details->m_fWrapX) {
                        x = lineStartX;
                        y += CHS_CHAR_HEIGHT * g_Details->m_vScale.y;
                    }

                    if (fnGInput_ParseToken) {
                        CRGBA color;
                        bool b1, b2;
                        fnGInput_ParseToken(text, color, b1, b2);
                    }
                    if (fnGInput_PrintSymbol) {
                        fnGInput_PrintSymbol(x, y);
                    }
                    x += btnWidth;
                    text = p + 1;
                    continue;
                }
                else
                {
                    text = p + 1;
                    continue;
                }
            }
            else
            {
                float charWidth = GetCharacterSize(*text);
                float curX = (g_Details->m_bCentre || g_Details->m_bRightJustify) ? 0.0f : x;
                float limit = g_Details->m_bCentre ? g_Details->m_fCentreSize : g_Details->m_fWrapX;
                if (g_Details->m_bBackground) {
                    if (curX + charWidth > limit) {
                        x = lineStartX;
                        y += CHS_CHAR_HEIGHT * g_Details->m_vScale.y;
                    }
                }
                PrintCharDispatcher(x, y, *text);
                x += charWidth;
                ++text;
            }
        }
    }

    __declspec(naked) static void LoadCHSGXT() {
        __asm {
            pop eax;
            add eax, 2;
            push 0x40000;
            push esi;
            push offset textPath;
            jmp eax;
        }
    }

    void LoadCHSTexture() {
        char chs_normal[] = "chs_normal";
        char chs_normal_mask[] = "chs_normal_mask";
        char chs_slant[] = "chs_slant";
        char chs_slant_mask[] = "chs_slant_mask";
        CTxdStore::PopCurrentTxd();
        int slot = CTxdStore::AddTxdSlot("wm_lcchs");
        CTxdStore::LoadTxd(slot, texturePath);
        CTxdStore::AddRef(slot);
        CTxdStore::PushCurrentTxd();
        CTxdStore::SetCurrentTxd(slot);
        g_ChsSprite.SetTexture(chs_normal, chs_normal_mask);
        g_ChsSlantSprite.SetTexture(chs_slant, chs_slant_mask);
        CTxdStore::PopCurrentTxd();
    }

    void UnloadCHSTexture(int dummy) {
        g_ChsSprite.Delete();
        g_ChsSlantSprite.Delete();
        CTxdStore::RemoveTxdSlot(CTxdStore::FindTxdSlot("wm_lcchs"));
    }

    void BuildPaths() {
        char pluginPath[260];
        GetModuleFileNameA(GetModuleHandleA("wm_lcchs.asi"), pluginPath, 260);
        strcpy(datPath, pluginPath);
        strcpy(textPath, pluginPath);
        strcpy(texturePath, pluginPath);
        strcpy(strrchr(datPath, '.'), "\\wm_lcchs.dat");
        strcpy(strrchr(textPath, '.'), "\\wm_lcchs.gxt");
        strcpy(strrchr(texturePath, '.'), "\\wm_lcchs.txd");
    }

    void InitGInput() {
        auto GInputHandle = GetModuleHandleW(L"GInputIII.asi");
        if (GInputHandle != NULL) {
            std::intptr_t base = reinterpret_cast<std::intptr_t>(GInputHandle);
            fnGInput_ParseToken = injector::auto_pointer(base + 0x6AA0);
            fnGInput_SkipToken = injector::auto_pointer(base + 0x7570);
            fnGInput_PrintSymbol = injector::auto_pointer(base + 0x5860);
            GInput_ButtonSymbol = injector::auto_pointer(base + 0x5A60);
            g_GInputAvailable = true;
        }
        else {
            fnGInput_ParseToken = nullptr;
            fnGInput_SkipToken = nullptr;
            fnGInput_PrintSymbol = nullptr;
            GInput_ButtonSymbol = nullptr;
            g_GInputAvailable = false;
        }
    }

    void InitAddresses() {
        g_Size = reinterpret_cast<CFontSizes*>(GLOBAL_ADDRESS_BY_VERSION(0x5FD120, 0x5FCF08, 0x609F00));
        g_Details = reinterpret_cast<CFontDetails*>(GLOBAL_ADDRESS_BY_VERSION(0x8F317C, 0x8F317C, 0x903370));
        g_fpPrintChar = reinterpret_cast<void*>(GLOBAL_ADDRESS_BY_VERSION(0x500C30, 0x500D10, 0x500CA0));
        g_fpParseToken = reinterpret_cast<void*>(GLOBAL_ADDRESS_BY_VERSION(0x5019A0, 0x501A80, 0x501A10));
        RsGlobalW = reinterpret_cast<int*>(GLOBAL_ADDRESS_BY_VERSION(0x8F436C, 0x8F4420, 0x904560));
        RsGlobalH = RsGlobalW + 1;

        uintptr_t parseTokenAddr = GLOBAL_ADDRESS_BY_VERSION(0x5019A0, 0x501A80, 0x501A10);
        g_GInputParseToken = (ParseTokenFn)injector::GetBranchDestination(parseTokenAddr).get();
        if (!g_GInputParseToken)
            g_GInputParseToken = (ParseTokenFn)parseTokenAddr;

        g_fpDrawBackground = reinterpret_cast<void(__cdecl*)(CRect*, void*)>(GLOBAL_ADDRESS_BY_VERSION(0x51F970, 0x51FA50, 0x51F9E0));
        g_pBackgroundParam = reinterpret_cast<void*>(GLOBAL_ADDRESS_BY_VERSION(0x8F31A0, 0x8F31A0, 0x903394));

        unsigned char* FET_LAN_Entry = reinterpret_cast<unsigned char*>(GLOBAL_ADDRESS_BY_VERSION(0x6157AC, 0x614F6C, 0x621F64));
        memcpy(FET_LAN_Entry, FET_LAN_Entry + 0x14, 0x14);
        memcpy(FET_LAN_Entry + 0x14, FET_LAN_Entry + 0x28, 0x14);
        memset(FET_LAN_Entry + 0x28, 0, 0x14);
    }

    void PatchGame() {
        injector::WriteMemory<unsigned int>(GLOBAL_ADDRESS_BY_VERSION(0x5082CF, 0x5083AF, 0x50833F), 255, true);
        injector::WriteMemory<unsigned char>(GLOBAL_ADDRESS_BY_VERSION(0x5082D4, 0x5083B4, 0x508344), 0, true);
        injector::WriteMemory<unsigned int>(GLOBAL_ADDRESS_BY_VERSION(0x5082D6, 0x5083B6, 0x508346), 0, true);
        injector::WriteMemory<unsigned char>(GLOBAL_ADDRESS_BY_VERSION(0x5082DB, 0x5083BB, 0x50834B), 0, true);
        injector::WriteMemory<short>(GLOBAL_ADDRESS_BY_VERSION(0x52B73A, 0x52B97A, 0x52B90A), 5, true);

        injector::MakeCALL(GLOBAL_ADDRESS_BY_VERSION(0x52C42F, 0x52C66F, 0x52C5FF), LoadCHSGXT);

        injector::MakeCALL(GLOBAL_ADDRESS_BY_VERSION(0x500B87, 0x500C67, 0x500BF7), LoadCHSTexture);
        injector::MakeCALL(GLOBAL_ADDRESS_BY_VERSION(0x500BCA, 0x500CAA, 0x500C3A), UnloadCHSTexture);
        injector::MakeCALL(GLOBAL_ADDRESS_BY_VERSION(0x50179F, 0x50187F, 0x50180F), PrintCharDispatcher);

        injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x5018A0, 0x501980, 0x501910), GetStringWidth);
        injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x501260, 0x501340, 0x5012D0), GetNumberLines);
        injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x5013B0, 0x501490, 0x501420), GetTextRect);
        injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x501960, 0x501A40, 0x5019D0), GetNextSpace);
        injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x501840, 0x501920, 0x5018B0), GetCharacterSize);
        injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x500F50, 0x501030, 0x501020), PrintStringHook);

        injector::MakeNOP(GLOBAL_ADDRESS_BY_VERSION(0x58F4EB, 0x58F7DB, 0x58F6CB), 6);
        injector::MakeNOP(GLOBAL_ADDRESS_BY_VERSION(0x58F4F3, 0x58F7E3, 0x58F6D3), 1);
        injector::MakeNOP(GLOBAL_ADDRESS_BY_VERSION(0x58F50D, 0x58F7FD, 0x58F6ED), 1);
        injector::MakeNOP(GLOBAL_ADDRESS_BY_VERSION(0x58F528, 0x58F818, 0x58F708), 1);
        injector::MakeNOP(GLOBAL_ADDRESS_BY_VERSION(0x58F52D, 0x58F81D, 0x58F70D), 6);
        injector::MakeNOP(GLOBAL_ADDRESS_BY_VERSION(0x58F55D, 0x58F84D, 0x58F73D), 6);
    }

    bool Init() {
        InitGInput();
        BuildPaths();
        ReadTable();
        LoadCHSTexture();
        InitAddresses();
        PatchGame();
        return true;
    }

    void Shutdown() {
        UnloadCHSTexture(0);
    }

} // namespace FontPatch