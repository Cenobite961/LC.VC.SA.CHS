#include "FontPatch.h"
#include <game_sa/CMessages.h>
#include <game_sa/CSprite.h>
#include <game_sa/CTheScripts.h>
#include <cstdio>
#include <cstring>
#include <RenderWare.h>
#include <injector.hpp>
#include <utility.hpp>
#include <utf8/unchecked.h>

#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

namespace FontPatch
{
    const float         scale_x_rec = 1.0f / 16.0f;
    const float         scale_y_rec = 1.0f / 12.8f;
    const float         scale_rec_chs = 1.0f / 64.0f;
    const float         fix_value = 0.0021f;
    const float         fix_value_chs = 0.0021f / 4.0f;
    const unsigned char SBCLetterWidth = 32;

    struct CFontRenderState
    {
    public:
        float     Useless;
        CVector2D Pos;
        CVector2D Scale;
        CRGBA     Color;
        float     JustifyWrap;
        float     Slant;
        CVector2D SlantRefPoint;
        bool      IsBlip;
        char      FontStyle;
        bool      Prop;
        char      _pad1;
        short     TextureID;
        char      OutlineSize;
        char      _pad2;
    };
    static_assert(sizeof(CFontRenderState) == 0x30);

    union FontBufferPointer {
        CFontRenderState* pdata;
        char* ptext;
        unsigned int      addr;
    };
    static_assert(sizeof(FontBufferPointer) == 0x4);

    struct CharPos
    {
        unsigned char rowIndex;
        unsigned char columnIndex;
    };

    CFontRenderState* m_FontBuffer = injector::auto_pointer(0xC716B0);
    FontBufferPointer* m_FontBufferIter = injector::auto_pointer(0xC716A8);
    CFontRenderState* RenderState = injector::auto_pointer(0xC71AA0);

    CSprite2d m_ChsSprite;

    unsigned char(__cdecl* FindSubFontCharacter)(unsigned char, unsigned char) = injector::auto_pointer(0x7192C0);
    void(__cdecl* RenderString)(float, float, const char*, const char*, float) = injector::auto_pointer(0x719B40);

    float* GInput_SymbolWidth;
    CSprite2d* GInput_ButtonSprites;
    char* (__cdecl* GInput_ParseTokenFunc)(char*, CRGBA&, bool, char*);
    char* (__stdcall* GInput_SkipToken)(char*, float*);
    int* GInput_BackLength;

    CharPos TheTable[0x10000];

    char texturePath[MAX_PATH];
    char textPath[MAX_PATH];

    char datPath[MAX_PATH];

    char aRb[] = "rb";
    __declspec(naked) void Hook_LoadGxt()
    {
        __asm
        {
            pop eax;
            inc eax;
            push offset aRb;
            push offset textPath;
            jmp eax;
        }
    }

    char* __stdcall WithoutGInputSkipToken(char* text, float* width)
    {
        if (text[0] == '~' && text[2] == '~')
        {
            *width = 0.0f;
            return text + 2;
        }
        return text;
    }

    void ReadTable()
    {
        memset(TheTable, 0xFF, sizeof(TheTable));

        FILE* hfile = fopen(datPath, "rb");
        if (hfile)
        {
            fseek(hfile, 0, SEEK_END);
            long size = ftell(hfile);
            if (size == 131072)
            {
                fseek(hfile, 0, SEEK_SET);
                fread(TheTable, 2, 0x10000, hfile);
            }
            fclose(hfile);
        }
    }

    CharPos GetCharPos(unsigned int chr)
    {
        CharPos result;

        if (chr < 0x60)
        {
            if (FontPatch::RenderState->FontStyle != 0)
            {
                chr = FindSubFontCharacter(chr, RenderState->FontStyle);

                if (chr == 0xD0)
                {
                    chr = 0;
                }
            }

            result.rowIndex = (chr >> 4);
            result.columnIndex = (chr & 0xF);
        }
        else
        {
            result = TheTable[chr];
        }

        return result;
    }

    float __cdecl GetScaledLetterWidthNormal(unsigned int arg_char)
    {
        unsigned char charWidth;

        if (arg_char >= 0x60)
        {
            charWidth = SBCLetterWidth;
        }
        else
        {
            if (arg_char == '?')
            {
                arg_char = 0;
            }

            if (CFont::m_FontStyle != 0)
            {
                arg_char = FindSubFontCharacter(arg_char, CFont::m_FontStyle);
            }

            if (CFont::m_bFontPropOn)
            {
                charWidth = gFontData[CFont::m_FontTextureId].m_propValues[arg_char];
            }
            else
            {
                charWidth = gFontData[CFont::m_FontTextureId].m_unpropValue;
            }
        }

        return ((charWidth + CFont::m_nFontOutlineSize) * CFont::m_Scale->x);
    }

    float GetScaledLetterWidthScript(unsigned int arg_char)
    {
        auto& drawer = CTheScripts::IntroTextLines[CTheScripts::NumberOfIntroTextLinesThisFrame];

        unsigned char charWidth, style;

        if (arg_char >= 0x60)
        {
            charWidth = SBCLetterWidth;
        }
        else
        {
            if (arg_char == '?')
            {
                arg_char = 0;
            }

            switch (drawer.font)
            {
            case 2:
                style = 0;
                arg_char = FindSubFontCharacter(arg_char, 2);
                break;

            case 3:
                style = 1;
                arg_char = FindSubFontCharacter(arg_char, 1);
                break;

            default:
                style = drawer.font;
                break;
            }

            if (drawer.proportional)
            {
                charWidth = gFontData[style].m_propValues[arg_char];
            }
            else
            {
                charWidth = gFontData[style].m_unpropValue;
            }
        }

        return ((charWidth + drawer.outlineType) * drawer.letterWidth);
    }

    float __cdecl GetScaledLetterWidthDrawing(unsigned int arg_char)
    {
        unsigned char charWidth;

        if (arg_char >= 0x60)
        {
            charWidth = SBCLetterWidth;
        }
        else
        {
            if (arg_char == '?')
            {
                arg_char = 0;
            }

            if (RenderState->FontStyle != 0)
            {
                arg_char = FindSubFontCharacter(arg_char, RenderState->FontStyle);
            }

            if (RenderState->Prop)
            {
                charWidth = gFontData[RenderState->TextureID].m_propValues[arg_char];
            }
            else
            {
                charWidth = gFontData[RenderState->TextureID].m_unpropValue;
            }
        }

        return (charWidth + RenderState->OutlineSize) * RenderState->Scale.x;
    }

    float GetStringWidth(const char* arg_text, bool bGetAll, bool bScript)
    {
        char  strbuf[400];
        char* bufIter = strbuf;

        float result = 0.0f;

        bool succeeded = false;
        bool StopAtDelim = false;

        strncpy(strbuf, arg_text, 400);
        strbuf[399] = 0;

        CMessages::InsertPlayerControlKeysInString(strbuf);

        while (true)
        {
            unsigned int code = utf8::unchecked::peek_next(bufIter);

            if (code == '\0')
            {
                break;
            }
            else if (code == ' ' && !bGetAll)
            {
                break;
            }
            else if (code == '~')
            {
                if (!bGetAll && (StopAtDelim || succeeded))
                {
                    break;
                }

                bufIter = GInput_SkipToken(bufIter, &result);

                ++bufIter;

                if (succeeded || *bufIter == '~')
                {
                    StopAtDelim = true;
                }
            }
            else if (code >= 0x80)
            {
                if (bGetAll || !succeeded)
                {
                    if (bScript)
                    {
                        result += GetScaledLetterWidthScript(code - 0x20);
                    }
                    else
                    {
                        result += GetScaledLetterWidthNormal(code - 0x20);
                    }

                    succeeded = true;
                }

                if (!bGetAll)
                {
                    break;
                }
            }
            else
            {
                if (!bGetAll && code == ' ' && StopAtDelim)
                {
                    break;
                }

                if (bScript)
                {
                    result += GetScaledLetterWidthScript(code - 0x20);
                }
                else
                {
                    result += GetScaledLetterWidthNormal(code - 0x20);
                }

                succeeded = true;
            }

            utf8::unchecked::next(bufIter);
        }

        return result;
    }

    char* GetNextSpace(char* arg_pointer)
    {
        char* var_pointer = arg_pointer;

        while (true)
        {
            unsigned int code = utf8::unchecked::peek_next(var_pointer);

            if (code == 0 || code == ' ' || code == '~')
            {
                break;
            }
            else if (code >= 0x80)
            {
                if (var_pointer == arg_pointer)
                {
                    utf8::unchecked::next(var_pointer);
                }

                break;
            }

            utf8::unchecked::next(var_pointer);
        }

        return var_pointer;
    }

    void PrintCHSChar(float arg_x, float arg_y, unsigned int arg_char)
    {
        CRect   rect;
        float   row, column;
        CharPos cpos;

        if (arg_y < 0.0f || RsGlobal.maximumHeight < arg_y || arg_x < 0.0f || RsGlobal.maximumWidth < arg_x)
        {
            return;
        }

        if (CFont::m_nExtraFontSymbolId != 0)
        {
            rect.top = RenderState->Scale.y * 2.0f + arg_y;
            rect.right = RenderState->Scale.y * *GInput_SymbolWidth + arg_x;
            rect.bottom = RenderState->Scale.y * 19.0f + arg_y;
            rect.left = arg_x;
            GInput_ButtonSprites[CFont::m_nExtraFontSymbolId].Draw(rect, CRGBA(255, 255, 255, RenderState->Color.a));
            return;
        }

        if (arg_char == 0 || arg_char == '?')
        {
            return;
        }

        cpos = GetCharPos(arg_char);

        row = cpos.rowIndex;
        column = cpos.columnIndex;

        rect.left = arg_x;
        rect.top = arg_y;
        rect.right = RenderState->Scale.x * 32.0f + arg_x;
        rect.bottom = RenderState->Scale.y * 20.0f + arg_y;

        if (arg_char < 0x60)
        {
            row *= scale_y_rec;
            column *= scale_x_rec;

            CSprite2d::AddToBuffer(rect, RenderState->Color, column, row + fix_value, column + scale_x_rec - fix_value,
                row + fix_value, column, row + scale_y_rec - fix_value, column + scale_x_rec - fix_value,
                row + scale_y_rec - fix_value);
        }
        else
        {
            row *= scale_rec_chs;
            column *= scale_rec_chs;

            CSprite2d::AddToBuffer(rect, RenderState->Color, column, row + fix_value_chs,
                column + scale_rec_chs - fix_value_chs, row + fix_value_chs, column,
                row + scale_rec_chs - fix_value_chs, column + scale_rec_chs - fix_value_chs,
                row + scale_rec_chs - fix_value_chs);
        }
    }

    short GetNumberLines(bool print, float arg_x, float arg_y, char* arg_text)
    {
        char* esi = arg_text;
        const char* ebp = esi;
        const char* edi;

        short result = 0;
        short numWords = 0;

        bool emptyLine = true;
        char tag = '\0';

        CRGBA fontColor = *CFont::m_Color;
        CRGBA tagColor;

        float xBound;
        float yBound = arg_y;
        float strWidth, widthLimit;
        float var_110 = 0.0f;
        float var_10C;
        float var_124;

        char var_100[256];

        if (CFont::m_bFontCentreAlign || CFont::m_bFontRightAlign)
        {
            xBound = 0.0f;
        }
        else
        {
            xBound = arg_x;
        }

        while (*esi != '\0')
        {
            CFont::m_nExtraFontSymbolId = 0;
            strWidth = GetStringWidth(esi, false, false);

            if (*esi == '~')
            {
                esi = GInput_ParseTokenFunc(esi, tagColor, true, &tag);
            }

            if (CFont::m_bFontCentreAlign)
            {
                widthLimit = CFont::m_fFontCentreSize;
            }
            else if (CFont::m_bFontRightAlign)
            {
                widthLimit = arg_x - CFont::m_fRightJustifyWrap;
            }
            else
            {
                widthLimit = CFont::m_fWrapx;
            }

            strWidth += xBound;

            if ((strWidth <= widthLimit || emptyLine) && !CFont::m_bNewLine)
            {
                xBound = strWidth;
                esi = GetNextSpace(esi);

                if (*esi != '\0')
                {
                    if (!emptyLine)
                    {
                        ++numWords;
                    }

                    if (*esi == ' ')
                    {
                        xBound += GetScaledLetterWidthNormal(0);
                        ++esi;
                    }

                    var_110 = xBound;
                    emptyLine = false;
                }
                else
                {
                    if (CFont::m_bFontCentreAlign)
                    {
                        var_124 = arg_x - xBound * 0.5f;
                    }
                    else if (CFont::m_bFontRightAlign)
                    {
                        var_124 = arg_x - xBound;
                    }
                    else
                    {
                        var_124 = arg_x;
                    }

                    ++result;

                    if (print)
                    {
                        RenderString(var_124, yBound, ebp, esi, 0.0f);
                    }
                }
            }
            else
            {
                var_10C = 0.0f;

                if (CFont::m_nExtraFontSymbolId != 0)
                {
                    esi -= *GInput_BackLength;
                }

                edi = esi - 3;

                if (!CFont::m_bNewLine)
                {
                    edi = esi;
                }

                if (CFont::m_bFontCentreAlign)
                {
                    var_124 = arg_x - xBound * 0.5f;
                }
                else
                {
                    if (CFont::m_bFontJustify)
                    {
                        var_10C = (CFont::m_fWrapx - var_110) / numWords;
                    }

                    if (CFont::m_bFontRightAlign)
                    {
                        var_124 = arg_x - (xBound - GetScaledLetterWidthNormal(0));
                    }
                    else
                    {
                        var_124 = arg_x;
                    }
                }

                ++result;

                if (print)
                {
                    RenderString(var_124, yBound, ebp, edi, var_10C);
                }

                if (tag != '\0')
                {
                    var_100[0] = '~';
                    var_100[1] = tag;
                    var_100[2] = '~';

                    if (CFont::m_bNewLine)
                    {
                        edi += 3;
                    }

                    strcpy(&var_100[3], edi);

                    esi = var_100;
                    tag = '\0';
                }

                CFont::m_bNewLine = false;
                yBound += CFont::m_Scale->y * 18.0f;

                if (CFont::m_bFontCentreAlign || CFont::m_bFontRightAlign)
                {
                    xBound = 0.0f;
                }
                else
                {
                    xBound = arg_x;
                }

                ebp = esi;
                emptyLine = true;
                var_110 = 0.0f;
                numWords = 0;
            }

            CFont::m_nExtraFontSymbolId = 0;
        };

        if (print)
        {
            CFont::SetColor(fontColor);
        }

        return result;
    }

    void RenderFontBuffer()
    {
        CRGBA        var_color;
        CVector2D    pos;
        unsigned int var_char;

        FontBufferPointer ebx;

        if (m_FontBufferIter->pdata == m_FontBuffer)
        {
            return;
        }

        RenderState = m_FontBuffer;

        var_color = RenderState->Color;

        pos = RenderState->Pos;

        ebx.pdata = m_FontBuffer + 1;

        while (ebx.addr < m_FontBufferIter->addr)
        {
            if (*ebx.ptext == '\0')
            {
                ++ebx.ptext;

                while ((ebx.addr & 3) != 0)
                {
                    ++ebx.ptext;
                }

                if (ebx.addr >= m_FontBufferIter->addr)
                {
                    break;
                }

                *RenderState = *ebx.pdata;

                var_color = RenderState->Color;

                pos = RenderState->Pos;

                ++ebx.pdata;
            }

            CFont::m_nExtraFontSymbolId = 0;

            while (*ebx.ptext == '~')
            {
                if (CFont::m_nExtraFontSymbolId != 0)
                {
                    break;
                }

                ebx.ptext = GInput_ParseTokenFunc(ebx.ptext, var_color, RenderState->IsBlip, nullptr);

                if (!RenderState->IsBlip)
                {
                    RenderState->Color = var_color;
                }
            }

            unsigned int raw_char = utf8::unchecked::peek_next(ebx.ptext);
            if (raw_char < 0x80) {
                var_char = raw_char - 0x20;
            }
            else {
                var_char = raw_char;
            }

            if (RenderState->Slant != 0.0f)
            {
                pos.y = (RenderState->SlantRefPoint.x - pos.x) * RenderState->Slant + RenderState->SlantRefPoint.y;
            }

            if (CFont::m_nExtraFontSymbolId == 0 || !RenderState->IsBlip)
            {
                if (var_char < 0x60)
                {
                    CFont::Sprite[RenderState->TextureID].SetRenderState();
                }
                else
                {
                    m_ChsSprite.SetRenderState();
                }

                RwRenderStateSet(RwRenderState::rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);

                PrintCHSChar(pos.x, pos.y, var_char);
                CSprite::FlushSpriteBuffer();
                CSprite2d::RenderVertexBuffer();
            }

            if (CFont::m_nExtraFontSymbolId == 0)
            {
                pos.x += GetScaledLetterWidthDrawing(var_char);
            }
            else
            {
                pos.x += (RenderState->Scale.y * *GInput_SymbolWidth + RenderState->OutlineSize);
            }

            if (var_char == 0)
            {
                pos.x += RenderState->JustifyWrap;
            }

            if (CFont::m_nExtraFontSymbolId != 0)
            {
                CFont::m_nExtraFontSymbolId = 0;
            }
            else if (*ebx.ptext != '\0')
            {
                utf8::unchecked::next(ebx.ptext);
            }
        }

        m_FontBufferIter->pdata = m_FontBuffer;
    }

    void InitGinput() {
        auto GInputHandle = GetModuleHandleW(L"GInputSA.asi");

        static float g_defaultSymbolWidth = 17.0f;
        static int   g_defaultBackLength = 3;

        if (GInputHandle != NULL)
        {
            std::intptr_t base = reinterpret_cast<std::intptr_t>(GInputHandle);
            GInput_SymbolWidth = injector::auto_pointer(base + 0x3B084).get();
            GInput_ButtonSprites = injector::auto_pointer(base + 0x3AD60).get();
            GInput_ParseTokenFunc = injector::auto_pointer(base + 0x9040).get();
            GInput_SkipToken = injector::auto_pointer(base + 0x99C0).get();
            GInput_BackLength = injector::auto_pointer(base + 0x3AE60).get();
        }
        else
        {
            GInput_ParseTokenFunc = CFont::ParseToken;
            GInput_SkipToken = WithoutGInputSkipToken;
            GInput_SymbolWidth = &g_defaultSymbolWidth;
            GInput_BackLength = &g_defaultBackLength;
            GInput_ButtonSprites = CFont::ButtonSprite;
        }
    }
    
    void BuildPath() {
        char pluginPath[MAX_PATH];

        GetModuleFileNameA(GetModuleHandleA("wm_sachs.asi"), pluginPath, MAX_PATH);
        strcpy(texturePath, pluginPath);
        strcpy(textPath, pluginPath);
        strcpy(datPath, pluginPath);
        strcpy(strrchr(texturePath, '.'), "\\wm_sachs.png");
        strcpy(strrchr(textPath, '.'), "\\wm_sachs.gxt");
        strcpy(strrchr(datPath, '.'), "\\wm_sachs.dat");
    }

    void ApplyTexture() {
        int width, height, depth, flags;

        RwImage* image = RtPNGImageRead(texturePath);
        RwImageFindRasterFormat(image, 4, &width, &height, &depth, &flags);
        RwRaster* raster = RwRasterCreate(width, height, depth, flags);
        RwRasterSetFromImage(raster, image);
        RwImageDestroy(image);
        m_ChsSprite.m_pTexture = RwTextureCreate(raster);
    }

    void PathGame() {
        injector::MakeCALL(0x69FD54, Hook_LoadGxt);
        injector::MakeCALL(0x6A0222, Hook_LoadGxt);
        injector::MemoryFill(0x8CFD6A, 0, 0x12, false);

        injector::MakeCALL(0x47B565, GetStringWidth);
        injector::MakeCALL(0x47B73A, GetStringWidth);
        injector::MakeCALL(0x57A49B, GetStringWidth);
        injector::MakeCALL(0x57FB52, GetStringWidth);
        injector::MakeCALL(0x57FE35, GetStringWidth);
        injector::MakeCALL(0x5814A7, GetStringWidth);
        injector::MakeCALL(0x581512, GetStringWidth);
        injector::MakeCALL(0x58BCCC, GetStringWidth);

        injector::MakeCALL(0x71A5F1, GetNumberLines);
        injector::MakeCALL(0x71A611, GetNumberLines);
        injector::MakeCALL(0x71A631, GetNumberLines);
        injector::MakeCALL(0x71A802, GetNumberLines);
        injector::MakeCALL(0x71A834, GetNumberLines);

        injector::MakeCALL(0x57BF70, RenderFontBuffer);
        injector::MakeCALL(0x719B5D, RenderFontBuffer);
        injector::MakeCALL(0x719F43, RenderFontBuffer);
        injector::MakeJMP(0x71A210, RenderFontBuffer);
    }

    bool Init()
    {
        InitGinput();
        BuildPath();
        ApplyTexture();
        ReadTable();
        PathGame();
        return true;
    }

    void Shutdown()
    {
        m_ChsSprite.Delete();
    }
} // namespace FontPatch