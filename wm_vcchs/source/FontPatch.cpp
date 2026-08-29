#include "FontPatch.h"
#include <array>
#include <vector>
#include <functional>

namespace FontPatch
{
	typedef wchar_t CharType;

	struct FontBufferEntry
	{
		CFontRenderState data;
		CharType         text[1];
	};

	union FontBufferPointer {
		CFontRenderState* pdata;
		CharType* ptext;
		std::uintptr_t    addr;
	};

	struct CharPos
	{
		unsigned char rowIndex;
		unsigned char columnIndex;
	};

	char fontPath[260];
	char textPath[260];
	char datPath[260];
	char iniPath[260];

	tFontTable* Size;
	FontBufferPointer  FontBuffer;
	FontBufferPointer* FontBufferIter;
	CFontDetails* Details;
	CFontRenderState* RenderState;
	CSprite2d* Sprite;

	CharType* (__cdecl* fnGInput_ParseToken)(CharType*, CRGBA&, bool&, bool&);
	CharType* (__stdcall* fnGInput_SkipToken)(CharType*, float*);
	int(__fastcall* fnGInput_SetRenderState)(CSprite2d*);
	float(__cdecl* fnGInput_PrintSymbol)(float, float);
	char* GInput_ButtonSymbol;
	CSprite2d* GInput_ButtonSprite;

	void(__cdecl* fnPrintChar)(float, float, CharType);
	CharType(__cdecl* fnFindNewCharacter)(CharType);
	void(__cdecl* fnPrintString)(float, float, unsigned int, const CharType*, const CharType*, float);

	CSprite2d ChsSprite;
	CSprite2d ChsSlantSprite;

	static bool g_SinicizationVersion = true;

	static bool g_GInputAvailable = false;

	constexpr float LINE_HEIGHT_FACTOR = 18.0f;
	constexpr float TEXT_RECT_PADDING = 4.0f;
	constexpr float TEXT_RECT_TOP_OFFSET = 2.0f;
	constexpr float TEXT_RECT_BOTTOM_OFFSET = 4.0f;

	constexpr float CHAR_WIDTH_FACTOR = 32.0f;
	constexpr float CHAR_HEIGHT_FACTOR = 16.0f;
	constexpr float SLANT_Y_OFFSET = 0.015f;

	constexpr float TEX_UFIX = 0.001f / 4.0f;
	constexpr float TEX_VFIX = 0.001f / 4.0f;
	constexpr float TEX_VFIX1_SLANT = 0.00055f / 4.0f;
	constexpr float TEX_VFIX2_SLANT = 0.007f / 4.0f;
	constexpr float TEX_VFIX3_SLANT = 0.009f / 4.0f;

	constexpr float GRID_ROWS = 64.0f;
	constexpr float GRID_COLS = 64.0f;
	constexpr float ROW_STEP = 1.0f / GRID_ROWS;
	constexpr float COL_STEP = 1.0f / GRID_COLS;

	constexpr short MAX_CHAR_WIDTH = 28;
	constexpr int TABLE_SIZE = 0x10000;

	constexpr float HALF = 0.5f;

	static std::array<CharPos, TABLE_SIZE> sTable;

	const CharPos& GetCharPos(CharType chr)
	{
		return sTable[chr];
	}

	void ReadIni()
	{
		g_SinicizationVersion = GetPrivateProfileIntA("Texture", "SinicizationVersion", 0, iniPath) == 0;
	}

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

	void LoadCHSFont()
	{
		char normal[] = "normal";
		char normalm[] = "normalm";
		char slant[] = "slant";
		char slantm[] = "slantm";

		int slot = CTxdStore::AddTxdSlot("wm_vcchs");
		CTxdStore::LoadTxd(slot, fontPath);
		CTxdStore::AddRef(slot);
		CTxdStore::PushCurrentTxd();
		CTxdStore::SetCurrentTxd(slot);
		ChsSprite.SetTexture(normal, normalm);
		ChsSlantSprite.SetTexture(slant, slantm);
		CTxdStore::PopCurrentTxd();
	}

	void UnloadCHSFont()
	{
		ChsSprite.Delete();
		ChsSlantSprite.Delete();
		CTxdStore::RemoveTxdSlot(CTxdStore::FindTxdSlot("wm_vcchs"));
	}

	float GetCharacterSize(CharType arg_char, short nFontStyle, bool bFontHalfTexture, bool bProp, float fScaleX)
	{
		short charWidth;

		if (arg_char >= 0x80)
		{
			charWidth = MAX_CHAR_WIDTH + 1;
		}
		else
		{
			arg_char -= 0x20;

			if (bFontHalfTexture)
			{
				arg_char = fnFindNewCharacter(arg_char);
			}

			if (bProp)
			{
				charWidth = Size[nFontStyle].prop[arg_char];
			}
			else
			{
				charWidth = Size[nFontStyle].unprop;
			}
		}

		return (charWidth * fScaleX);
	}

	float GetCharacterSizeNormal(CharType arg_char)
	{
		return GetCharacterSize(arg_char, Details->m_FontStyle, Details->m_bFontHalfTexture, Details->m_bPropOn,
			Details->m_vecScale.x);
	}

	float GetCharacterSizeDrawing(CharType arg_char)
	{
		return GetCharacterSize(arg_char, RenderState->FontStyle, RenderState->bFontHalfTexture, RenderState->bProp,
			RenderState->fTextSizeX);
	}

	float GetStringWidth(CharType* arg_text, bool bGetAll)
	{
		float result = 0.0f;
		while (*arg_text != '\0')
		{
			if (*arg_text == ' ')
			{
				if (bGetAll)
					result += GetCharacterSizeNormal(' ');
				else
					break;
			}
			else if (*arg_text == '~')
			{
				if (result == 0.0f || bGetAll)
				{
					if (fnGInput_SkipToken)
					{
						arg_text = fnGInput_SkipToken(arg_text, &result);
						result *= 2;
					}
					else
					{
						do
						{
							++arg_text;
						} while (*arg_text != '~');
					}
				}
				else
					break;
			}
			else if (*arg_text < 0x80)
			{
				result += GetCharacterSizeNormal(*arg_text);
			}
			else
			{
				if (result == 0.0f || bGetAll)
					result += GetCharacterSizeNormal(*arg_text);
				if (!bGetAll)
					break;
			}
			++arg_text;
		}
		return result;
	}

	CharType* GetNextSpace(CharType* arg_text)
	{
		CharType* temp = arg_text;
		while (*temp != ' ' && *temp != '\0')
		{
			if (*temp == '~')
			{
				if (temp == arg_text)
				{
					float symbolWidth = 0.0f;
					if (fnGInput_SkipToken)
					{
						temp = fnGInput_SkipToken(temp, &symbolWidth);
						++temp;
						if (g_GInputAvailable && GInput_ButtonSymbol && *GInput_ButtonSymbol != 0)
							break;
					}
					else
					{
						do
						{
							++temp;
						} while (*temp != '~');
						++temp;
					}

					arg_text = temp;
					continue;
				}
				else
					break;
			}
			else if (*temp >= 0x80)
			{
				if (temp == arg_text)
					++temp;
				break;
			}
			++temp;
		}
		return temp;
	}

	short GetNumberLines(float arg_x, float arg_y, CharType* arg_text)
	{
		short result = 0;
		float xBound;
		float yBound = arg_y;
		float strWidth, widthLimit;

		if (Details->m_bCentre || Details->m_bRightJustify)
		{
			xBound = 0.0f;
		}
		else
		{
			xBound = arg_x;
		}

		while (*arg_text != 0)
		{
			strWidth = GetStringWidth(arg_text, false);

			if (Details->m_bCentre)
			{
				widthLimit = Details->m_fCentreSize;
			}
			else
			{
				widthLimit = Details->m_fWrapX;
			}

			if ((xBound + strWidth) <= widthLimit)
			{
				xBound += strWidth;
				arg_text = GetNextSpace(arg_text);

				if (*arg_text == ' ')
				{
					xBound += GetCharacterSizeNormal(' ');
					++arg_text;
				}
				else if (*arg_text == 0)
				{
					++result;
				}
			}
			else
			{
				if (Details->m_bCentre || Details->m_bRightJustify)
				{
					xBound = 0.0f;
				}
				else
				{
					xBound = arg_x;
				}

				++result;
				yBound += Details->m_vecScale.y * LINE_HEIGHT_FACTOR;
			}
		}

		return result;
	}

	void GetTextRect(CRect* result, float arg_x, float arg_y, CharType* arg_text)
	{
		short numLines = GetNumberLines(arg_x, arg_y, arg_text);

		if (Details->m_bCentre)
		{
			if (Details->m_bBackGroundOnlyText)
			{
				result->left = arg_x - TEXT_RECT_PADDING;
				result->right = arg_x + TEXT_RECT_PADDING;
				result->bottom = (LINE_HEIGHT_FACTOR * Details->m_vecScale.y) * numLines + arg_y + TEXT_RECT_TOP_OFFSET;
				result->top = arg_y - TEXT_RECT_TOP_OFFSET;
			}
			else
			{
				result->left = arg_x - (Details->m_fCentreSize * HALF) - TEXT_RECT_PADDING;
				result->right = arg_x + (Details->m_fCentreSize * HALF) + TEXT_RECT_PADDING;
				result->bottom = arg_y + (LINE_HEIGHT_FACTOR * Details->m_vecScale.y * numLines) + TEXT_RECT_TOP_OFFSET;
				result->top = arg_y - TEXT_RECT_TOP_OFFSET;
			}
		}
		else
		{
			result->left = arg_x - TEXT_RECT_PADDING;
			result->right = Details->m_fWrapX;
			result->bottom = arg_y;
			result->top = (LINE_HEIGHT_FACTOR * Details->m_vecScale.y) * numLines + arg_y + TEXT_RECT_PADDING;
		}
	}

	void PrintString(float arg_x, float arg_y, CharType* arg_text)
	{
		static const CharType bordersText[] = L"BORDERS";
		static const CharType wmReplaceText[] = L"宽屏模式";
		static const CharType zbReplaceText[] = L"宽屏幕";

		if (arg_text && wcscmp(arg_text, bordersText) == 0) {
			arg_text = const_cast<CharType*>(g_SinicizationVersion ? wmReplaceText : zbReplaceText);
		}
		if (!g_SinicizationVersion && arg_text && wcscmp(arg_text, wmReplaceText) == 0) {
			arg_text = const_cast<CharType*>(zbReplaceText);
		}

		if (!arg_text || *arg_text == '*') return;
		size_t len = wcslen(arg_text);
		std::vector<CharType> buffer(len + 1);
		wcscpy(buffer.data(), arg_text);
		CharType* textPtr = buffer.data();

		CRect textBoxRect;

		float xBound;
		float yBound = arg_y;
		float strWidth, widthLimit;
		float var_38 = 0.0f;
		float print_x;
		float justifyWrap;

		CharType* ptext = textPtr;
		CharType* strHead = textPtr;

		bool emptyLine = true;

		short numSpaces = 0;

		Details->field_1F = 0;

		if (*textPtr == '*')
		{
			return;
		}

		++Details->field_50;

		if (Details->m_bBackground)
		{
			GetTextRect(&textBoxRect, arg_x, arg_y, textPtr);
			CSprite2d::DrawRect(textBoxRect, Details->m_BackgroundColor);
		}

		if (Details->m_bCentre || Details->m_bRightJustify)
		{
			xBound = 0.0f;
		}
		else
		{
			xBound = arg_x;
		}

		while (*ptext != 0)
		{
			strWidth = GetStringWidth(ptext, false);

			if (Details->m_bCentre)
			{
				widthLimit = Details->m_fCentreSize;
			}
			else if (Details->m_bRightJustify)
			{
				widthLimit = arg_x - Details->m_fRightJustifyWrap;
			}
			else
			{
				widthLimit = Details->m_fWrapX;
			}

			if (((xBound + strWidth) <= widthLimit) || emptyLine)
			{
				ptext = GetNextSpace(ptext);
				xBound += strWidth;

				if (*ptext != 0)
				{
					if (*ptext == ' ')
					{
						if (*(ptext + 1) == 0)
						{
							*ptext = 0;
						}
						else
						{
							if (!emptyLine)
							{
								++numSpaces;
							}

							xBound += GetCharacterSizeNormal(' ');
							++ptext;
						}
					}

					emptyLine = false;

					var_38 = xBound;
				}
				else
				{
					if (Details->m_bCentre)
					{
						print_x = arg_x - xBound * HALF;
					}
					else if (Details->m_bRightJustify)
					{
						print_x = arg_x - xBound;
					}
					else
					{
						print_x = arg_x;
					}

					fnPrintString(print_x, yBound, 0, strHead, ptext, 0.0f);
				}
			}
			else
			{
				if (Details->m_bJustify && !(Details->m_bCentre))
				{
					justifyWrap = (Details->m_fWrapX - var_38) / numSpaces;
				}
				else
				{
					justifyWrap = 0.0f;
				}

				if (Details->m_bCentre)
				{
					print_x = arg_x - xBound * HALF;
				}
				else if (Details->m_bRightJustify)
				{
					print_x = arg_x - xBound;
				}
				else
				{
					print_x = arg_x;
				}

				fnPrintString(print_x, yBound, 0, strHead, ptext, justifyWrap);

				strHead = ptext;

				if (Details->m_bCentre || Details->m_bRightJustify)
				{
					xBound = 0.0f;
				}
				else
				{
					xBound = arg_x;
				}

				yBound += Details->m_vecScale.y * LINE_HEIGHT_FACTOR;
				var_38 = 0.0f;
				numSpaces = 0;
				emptyLine = true;
			}
		}
	}

	void PrintCHSChar(float arg_x, float arg_y, CharType arg_char)
	{
		CRect rect;

		float yOffset;

		float u1, v1, u2, v2, u3, v3, u4, v4;

		CharPos pos;

		if (arg_x >= RsGlobal.maximumWidth || arg_x <= 0.0f || arg_y <= 0.0f || arg_y >= RsGlobal.maximumHeight)
		{
			return;
		}

		pos = GetCharPos(arg_char);
		yOffset = RenderState->fTextSizeY * TEXT_RECT_TOP_OFFSET;

		if (RenderState->fSlant == 0.0f)
		{
			rect.left = arg_x;
			rect.top = arg_y + yOffset;
			rect.right = RenderState->fTextSizeX * CHAR_WIDTH_FACTOR + arg_x;
			rect.bottom = RenderState->fTextSizeY * CHAR_HEIGHT_FACTOR + arg_y + yOffset;

			u1 = pos.columnIndex * COL_STEP;
			v1 = pos.rowIndex * ROW_STEP;
			u2 = (pos.columnIndex + 1) * COL_STEP - TEX_UFIX;
			v2 = v1;
			u3 = u1;
			v3 = (pos.rowIndex + 1) * ROW_STEP - TEX_VFIX;
			u4 = u2;
			v4 = v3;
		}
		else
		{
			rect.left = arg_x;
			rect.top = arg_y + SLANT_Y_OFFSET + yOffset;
			rect.right = RenderState->fTextSizeX * CHAR_WIDTH_FACTOR + arg_x;
			rect.bottom = RenderState->fTextSizeY * CHAR_HEIGHT_FACTOR + arg_y + yOffset;

			u1 = pos.columnIndex * COL_STEP;
			v1 = pos.rowIndex * ROW_STEP + TEX_VFIX1_SLANT;
			u2 = (pos.columnIndex + 1) * COL_STEP - TEX_UFIX;
			v2 = pos.rowIndex * ROW_STEP + TEX_VFIX + TEX_VFIX2_SLANT;
			u3 = pos.columnIndex * COL_STEP;
			v3 = (pos.rowIndex + 1) * ROW_STEP - TEX_VFIX3_SLANT;
			u4 = (pos.columnIndex + 1) * COL_STEP - TEX_UFIX;
			v4 = (pos.rowIndex + 1) * ROW_STEP + TEX_VFIX2_SLANT - TEX_VFIX;
		}

		CSprite2d::AddToBuffer(rect, RenderState->color, u1, v1, u2, v2, u3, v3, u4, v4);
	}

	void PrintCharDispatcher(float arg_x, float arg_y, CharType arg_char)
	{
		if (arg_char < 0x80)
		{
			arg_char -= 0x20;
			if (RenderState->bFontHalfTexture)
				arg_char = fnFindNewCharacter(arg_char);
			fnPrintChar(arg_x, arg_y, arg_char);
		}
		else
		{
			if (g_GInputAvailable && GInput_ButtonSymbol && *GInput_ButtonSymbol != 0 && fnGInput_PrintSymbol)
				arg_x += fnGInput_PrintSymbol(arg_x, arg_y);

			PrintCHSChar(arg_x, arg_y, arg_char);
		}
	}

	static inline void SetFontTexture(CSprite2d* sprite)
	{
		if (sprite && sprite->m_pTexture)
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)sprite->m_pTexture->raster);
	}

	void RenderFontBuffer()
	{
		bool var_D = false;
		bool var_E = false;

		CRGBA var_14;
		CVector2D pos;
		CharType var_char;
		FontBufferPointer pbuffer;

		if (FontBufferIter->addr == FontBuffer.addr)
			return;

		*RenderState = *(FontBuffer.pdata);
		var_14 = FontBuffer.pdata->color;

		pos.x = RenderState->fTextPosX;
		pos.y = RenderState->fTextPosY;

		pbuffer.addr = FontBuffer.addr + 0x30;

		while (pbuffer.addr < FontBufferIter->addr)
		{
			float symbolWidth = 0.0f;

			if (*(pbuffer.ptext) == 0)
			{
				++pbuffer.ptext;
				if ((pbuffer.addr & 3) != 0)
					++pbuffer.ptext;

				if (pbuffer.addr >= FontBufferIter->addr)
					break;

				*RenderState = *pbuffer.pdata;
				var_14 = RenderState->color;
				pos.x = RenderState->fTextPosX;
				pos.y = RenderState->fTextPosY;
				pbuffer.addr += 0x30;
			}

			if (*pbuffer.ptext == '~')
			{
				if (g_GInputAvailable && fnGInput_SkipToken && fnGInput_ParseToken)
				{
					fnGInput_SkipToken(pbuffer.ptext, &symbolWidth);
					pbuffer.ptext = fnGInput_ParseToken(pbuffer.ptext, var_14, var_E, var_D);
				}
				else
				{
					pbuffer.ptext = CFont::ParseToken(pbuffer.ptext, var_14, var_E, var_D);
				}

				if (var_E)
				{
					if ((CTimer::m_snTimeInMilliseconds - Details->field_48) > 300)
					{
						Details->field_46 = true;
						Details->field_48 = CTimer::m_snTimeInMilliseconds;
					}
					Details->m_Color.a = Details->field_46 ? 0 : 255;
				}

				if (!RenderState->bIsShadow)
					RenderState->color = var_14;
			}

			if (RenderState->fSlant != 0.0f)
				pos.y = (RenderState->fSlantRefPointX - pos.x) * RenderState->fSlant + RenderState->fSlantRefPointY;

			var_char = *pbuffer.ptext;

			bool bSlant = (RenderState->fSlant != 0.0f);

			if (var_char < 0x80)
			{
				if (fnGInput_SetRenderState)
					fnGInput_SetRenderState(&Sprite[RenderState->FontStyle]);
				else
					SetFontTexture(&Sprite[RenderState->FontStyle]);
			}
			else
			{
				CSprite2d* pSprite = bSlant ? &ChsSprite : &ChsSlantSprite;

				if (fnGInput_SetRenderState)
					fnGInput_SetRenderState(pSprite);
				else
					SetFontTexture(pSprite);
			}

			RwRenderStateSet(RwRenderState::rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);

			PrintCharDispatcher(pos.x, pos.y, var_char);

			if (var_D)
			{
				PrintCharDispatcher(pos.x + 1.0f, pos.y, var_char);
				PrintCharDispatcher(pos.x + 2.0f, pos.y, var_char);
				pos.x += 2.0f;
			}

			CSprite2d::RenderVertexBuffer();

			pos.x += GetCharacterSizeDrawing(var_char);
			if (var_char == ' ')
				pos.x += RenderState->fExtraSpace;
			else
				pos.x += symbolWidth;

			++pbuffer.ptext;
		}

		FontBufferIter->addr = FontBuffer.addr;
	}

	__declspec(naked) void hook_load_gxt_mission()
	{
		__asm
		{
			pop eax;
			add eax, 3;
			push offset textPath;
			jmp eax;
		}
	}

	char aRb[] = "rb";
	__declspec(naked) void hook_load_gxt()
	{
		__asm
		{
			pop eax;
			add eax, 5;
			push offset aRb;
			push offset textPath;
			jmp eax;
		}
	}

	static void InitGInput()
	{
		auto GInputHandle = GetModuleHandleW(L"GInputVC.asi");

		if (GInputHandle != NULL)
		{

			std::intptr_t base = reinterpret_cast<std::intptr_t>(GInputHandle);

			fnGInput_ParseToken = injector::auto_pointer(base + 0x71C0);
			fnGInput_SkipToken = injector::auto_pointer(base + 0x7B60);
			fnGInput_SetRenderState = injector::auto_pointer(base + 0x7C60);
			fnGInput_PrintSymbol = injector::auto_pointer(base + 0x6060);
			GInput_ButtonSymbol = injector::auto_pointer(base + 0x40D36);
			GInput_ButtonSprite = injector::auto_pointer(base + 0x40E80);
			g_GInputAvailable = true;
		}
		else
		{
			fnGInput_ParseToken = nullptr;
			fnGInput_SkipToken = nullptr;
			fnGInput_SetRenderState = nullptr;
			fnGInput_PrintSymbol = nullptr;
			GInput_ButtonSymbol = nullptr;
			GInput_ButtonSprite = nullptr;
			g_GInputAvailable = false;
		}
	}

	static void BuildPaths()
	{
		char pluginPath[260];
		GetModuleFileNameA(GetModuleHandleA("wm_vcchs.asi"), pluginPath, 260);
		std::strcpy(iniPath, pluginPath);
		std::strcpy(datPath, pluginPath);
		std::strcpy(fontPath, pluginPath);
		std::strcpy(textPath, pluginPath);

		std::strcpy(std::strrchr(iniPath, '.'), ".ini");
		std::strcpy(std::strrchr(datPath, '.'), "\\wm_vcchs.dat");
		std::strcpy(std::strrchr(fontPath, '.'), "\\wm_vcchs.txd");

		ReadIni();

		std::strcpy(std::strrchr(textPath, '.'), g_SinicizationVersion ? "\\wm_vcchs.gxt" : "\\zb_vcchs.gxt");
	}

	static void LoadGameAddresses()
	{
		Size = injector::auto_pointer(GLOBAL_ADDRESS_BY_VERSION(0x696BD8, 0x696BD8, 0x695BE0));
		FontBuffer.pdata = injector::auto_pointer(GLOBAL_ADDRESS_BY_VERSION(0x70935C, 0x70935C, 0x70835C));
		FontBufferIter = injector::auto_pointer(GLOBAL_ADDRESS_BY_VERSION(0x70975C, 0x70975C, 0x70875C));

		Details = injector::auto_pointer(GLOBAL_ADDRESS_BY_VERSION(0x97F820, 0x97F828, 0x97E828));
		RenderState = injector::auto_pointer(GLOBAL_ADDRESS_BY_VERSION(0x94B8F8, 0x94B900, 0x94A900));
		Sprite = injector::auto_pointer(GLOBAL_ADDRESS_BY_VERSION(0xA108B4, 0xA108BC, 0xA0F8BC));
		fnPrintChar = injector::auto_pointer(GLOBAL_ADDRESS_BY_VERSION(0x551E70, 0x551E90, 0x551D60));
		fnFindNewCharacter = injector::auto_pointer(GLOBAL_ADDRESS_BY_VERSION(0x54FE70, 0x54FE90, 0x54FD60));
		fnPrintString = injector::auto_pointer(GLOBAL_ADDRESS_BY_VERSION(0x5516C0, 0x5516E0, 0x5515B0));

		unsigned char* FEO_LAN_Entry =
			reinterpret_cast<unsigned char*>(GLOBAL_ADDRESS_BY_VERSION(0x6DA386, 0x6DA35E, 0x6D9356));
		memcpy(FEO_LAN_Entry, FEO_LAN_Entry + 0x12, 0x12);
		memcpy(FEO_LAN_Entry + 0x12, FEO_LAN_Entry + 0x24, 0x12);
		memset(FEO_LAN_Entry + 0x24, 0, 0x12);
	}

	static void ApplyHooks()
	{
		injector::MakeCALL(GLOBAL_ADDRESS_BY_VERSION(0x5852A0, 0x5852C0, 0x5850D0), hook_load_gxt_mission);
		injector::MakeCALL(GLOBAL_ADDRESS_BY_VERSION(0x5855EE, 0x58560E, 0x58541E), hook_load_gxt);

		injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x550650, 0x550670, 0x550540), GetStringWidth);
		injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x550720, 0x550740, 0x550610), GetTextRect);
		injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x550C70, 0x550C90, 0x550B60), GetNumberLines);

		injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x551040, 0x551060, 0x550F30), PrintString);
		injector::MakeJMP(GLOBAL_ADDRESS_BY_VERSION(0x551A30, 0x551A50, 0x551920), RenderFontBuffer);

		injector::WriteMemory(GLOBAL_ADDRESS_BY_VERSION(0x68FD58, 0x68FD58, 0x68ED60), 999.0f);

		unsigned char* SpaceAddInstr =
			reinterpret_cast<unsigned char*>(GLOBAL_ADDRESS_BY_VERSION(0x6161BB, 0x61619B, 0x615DDB));
		injector::MakeNOP(SpaceAddInstr, 6);
		injector::MakeNOP(SpaceAddInstr + 8);
		injector::MakeNOP(SpaceAddInstr + 0x22);
		injector::MakeNOP(SpaceAddInstr + 0x3D);
		injector::MakeNOP(SpaceAddInstr + 0x42, 6);
		injector::WriteMemory<unsigned char>(SpaceAddInstr + 0x65, 1, true);
		injector::MakeNOP(SpaceAddInstr + 0x72, 6);
	}

	bool Init()
	{
		InitGInput();
		BuildPaths();
		ReadTable();
		LoadCHSFont();
		LoadGameAddresses();
		ApplyHooks();
		return true;
	}

	void Shutdown()
	{
		UnloadCHSFont();
	}
} // namespace FontPatch