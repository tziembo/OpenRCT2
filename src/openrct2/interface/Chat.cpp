#pragma region Copyright (c) 2014-2017 OpenRCT2 Developers
/*****************************************************************************
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * OpenRCT2 is the work of many authors, a full list can be found in contributors.md
 * For more information, visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A full copy of the GNU General Public License can be found in licence.txt
 *****************************************************************************/
#pragma endregion

#include <algorithm>
#include "../audio/audio.h"
#include "../audio/AudioMixer.h"
#include "../Context.h"
#include "../interface/themes.h"
#include "../localisation/Localisation.h"
#include "../network/network.h"
#include "../platform/platform.h"
#include "../util/Util.h"
#include "Chat.h"

extern "C"
{

bool gChatOpen = false;
static char _chatCurrentLine[CHAT_MAX_MESSAGE_LENGTH];
static char _chatHistory[CHAT_HISTORY_SIZE][CHAT_INPUT_SIZE];
static uint32 _chatHistoryTime[CHAT_HISTORY_SIZE];
static uint32 _chatHistoryIndex = 0;
static uint32 _chatCaretTicks = 0;
static sint32 _chatLeft;
static sint32 _chatTop;
static sint32 _chatRight;
static sint32 _chatBottom;
static sint32 _chatWidth;
static sint32 _chatHeight;
static TextInputSession * _chatTextInputSession;

static const char* chat_history_get(uint32 index);
static uint32 chat_history_get_time(uint32 index);
static void chat_clear_input();

void chat_open()
{
    gChatOpen = true;
    _chatTextInputSession = context_start_text_input(_chatCurrentLine, sizeof(_chatCurrentLine));
}

void chat_close()
{
    gChatOpen = false;
    context_stop_text_input();
}

void chat_toggle()
{
    if (gChatOpen) {
        chat_close();
    } else {
        chat_open();
    }
}

void chat_init()
{
    memset(_chatHistory, 0, sizeof(_chatHistory));
    memset(_chatHistoryTime, 0, sizeof(_chatHistoryTime));
}

void chat_update()
{
    // Flash the caret
    _chatCaretTicks = (_chatCaretTicks + 1) % 30;
}

void chat_draw(rct_drawpixelinfo * dpi)
{
    if (network_get_mode() == NETWORK_MODE_NONE || network_get_status() != NETWORK_STATUS_CONNECTED || network_get_authstatus() != NETWORK_AUTH_OK) {
        gChatOpen = false;
        return;
    }

    _chatLeft = 10;
    _chatRight = std::min((context_get_width() - 10), CHAT_MAX_WINDOW_WIDTH);
    _chatWidth = _chatRight - _chatLeft;
    _chatBottom = context_get_height() - 45;
    _chatTop = _chatBottom - 10;

    char lineBuffer[CHAT_INPUT_SIZE + 10];
    char* lineCh = lineBuffer;
    char* inputLine = _chatCurrentLine;
    sint32 inputLineHeight = 10;
    uint8 chatBackgroundColor = theme_get_colour(WC_CHAT, 0);

    // Draw chat window
    if (gChatOpen) {
        inputLineHeight = chat_string_wrapped_get_height((void*)&inputLine, _chatWidth - 10);
        _chatTop -= inputLineHeight;

        for (sint32 i = 0; i < CHAT_HISTORY_SIZE; i++) {
            if (strlen(chat_history_get(i)) == 0) {
                continue;
            }

            safe_strcpy(lineBuffer, chat_history_get(i), sizeof(lineBuffer));

            sint32 lineHeight = chat_string_wrapped_get_height((void*)&lineCh, _chatWidth - 10);
            _chatTop -= (lineHeight + 5);
        }

        _chatHeight = _chatBottom - _chatTop;

        if (_chatTop < 50) {
            _chatTop = 50;
        } else if (_chatHeight < 150) { // Min height
            _chatTop = _chatBottom - 150;
            _chatHeight = 150;
        }

        gfx_set_dirty_blocks(_chatLeft, _chatTop - 5, _chatRight, _chatBottom + 5); // Background area + Textbox
        gfx_filter_rect(dpi, _chatLeft, _chatTop - 5, _chatRight, _chatBottom + 5, PALETTE_51); // Opaque gray background
        gfx_fill_rect_inset(dpi, _chatLeft, _chatTop - 5, _chatRight, _chatBottom + 5, chatBackgroundColor, INSET_RECT_FLAG_FILL_NONE);
        gfx_fill_rect_inset(dpi, _chatLeft + 1, _chatTop - 4, _chatRight - 1, _chatBottom - inputLineHeight - 6, chatBackgroundColor, INSET_RECT_FLAG_BORDER_INSET);
        gfx_fill_rect_inset(dpi, _chatLeft + 1, _chatBottom - inputLineHeight - 5, _chatRight - 1, _chatBottom + 4, chatBackgroundColor, INSET_RECT_FLAG_BORDER_INSET); // Textbox
    }

    sint32 x = _chatLeft + 5;
    sint32 y = _chatBottom - inputLineHeight - 20;
    sint32 stringHeight = 0;

    // Draw chat history
    for (sint32 i = 0; i < CHAT_HISTORY_SIZE; i++, y -= stringHeight) {
        uint32 expireTime = chat_history_get_time(i) + 10000;
        if (!gChatOpen && platform_get_ticks() > expireTime) {
            break;
        }

        safe_strcpy(lineBuffer, chat_history_get(i), sizeof(lineBuffer));

        stringHeight = chat_history_draw_string(dpi, (void*) &lineCh, x, y, _chatWidth - 10) + 5;
        gfx_set_dirty_blocks(x, y - stringHeight, x + _chatWidth, y + 20);

        if ((y - stringHeight) < 50) {
            break;
        }
    }

    // Draw current chat input
    if (gChatOpen) {
        lineCh = utf8_write_codepoint(lineCh, FORMAT_OUTLINE);
        lineCh = utf8_write_codepoint(lineCh, FORMAT_CELADON);

        safe_strcpy(lineCh, _chatCurrentLine, sizeof(_chatCurrentLine));
        y = _chatBottom - inputLineHeight - 5;

        lineCh = lineBuffer;
        inputLineHeight = gfx_draw_string_left_wrapped(dpi, (void*)&lineCh, x, y + 3, _chatWidth - 10, STR_STRING, TEXT_COLOUR_255);
        gfx_set_dirty_blocks(x, y, x + _chatWidth, y + inputLineHeight + 15);

        // TODO: Show caret if the input text has multiple lines
        if (_chatCaretTicks < 15 && gfx_get_string_width(lineBuffer) < (_chatWidth - 10)) {
            memcpy(lineBuffer, _chatCurrentLine, _chatTextInputSession->SelectionStart);
            lineBuffer[_chatTextInputSession->SelectionStart] = 0;
            sint32 caretX = x + gfx_get_string_width(lineBuffer);
            sint32 caretY = y + 14;

            gfx_fill_rect(dpi, caretX, caretY, caretX + 6, caretY + 1, PALETTE_INDEX_56);
        }
    }
}

void chat_history_add(const char * src)
{
    size_t bufferSize = strlen(src) + 64;
    utf8 * buffer = (utf8 *)calloc(1, bufferSize);

    // Find the start of the text (after format codes)
    const char * ch = src;
    const char * nextCh;
    uint32 codepoint;
    while ((codepoint = utf8_get_next(ch, &nextCh)) != 0) {
        if (!utf8_is_format_code(codepoint)) {
            break;
        }
        ch = nextCh;
    }
    const char * srcText = ch;

    // Copy format codes to buffer
    memcpy(buffer, src, std::min(bufferSize, (size_t)(srcText - src)));

    // Prepend a timestamp
    time_t timer;
    time(&timer);
    struct tm * tmInfo = localtime(&timer);

    strcatftime(buffer, bufferSize, "[%H:%M] ", tmInfo);
    safe_strcat(buffer, srcText, bufferSize);

    // Add to history list
    sint32 index = _chatHistoryIndex % CHAT_HISTORY_SIZE;
    memset(_chatHistory[index], 0, CHAT_INPUT_SIZE);
    memcpy(_chatHistory[index], buffer, std::min<size_t>(strlen(buffer), CHAT_INPUT_SIZE - 1));
    _chatHistoryTime[index] = platform_get_ticks();
    _chatHistoryIndex++;

    // Log to file (src only as logging does its own timestamp)
    network_append_chat_log(src);

    free(buffer);

    Mixer_Play_Effect(SOUND_NEWS_ITEM, 0, MIXER_VOLUME_MAX, 0.5f, 1.5f, true);
}

void chat_input(CHAT_INPUT input)
{
    switch (input) {
    case CHAT_INPUT_SEND:
        if (strlen(_chatCurrentLine) > 0) {
            network_send_chat(_chatCurrentLine);
        }
        chat_clear_input();
        chat_close();
        break;
    case CHAT_INPUT_CLOSE:
        chat_close();
        break;
    default:
        break;
    }
}

static const char* chat_history_get(uint32 index)
{
    return _chatHistory[(_chatHistoryIndex + CHAT_HISTORY_SIZE - index - 1) % CHAT_HISTORY_SIZE];
}

static uint32 chat_history_get_time(uint32 index)
{
    return _chatHistoryTime[(_chatHistoryIndex + CHAT_HISTORY_SIZE - index - 1) % CHAT_HISTORY_SIZE];
}

static void chat_clear_input()
{
    _chatCurrentLine[0] = 0;
}

// This method is the same as gfx_draw_string_left_wrapped.
// But this adjusts the initial Y coordinate depending of the number of lines.
sint32 chat_history_draw_string(rct_drawpixelinfo *dpi, void *args, sint32 x, sint32 y, sint32 width)
{
    sint32 fontSpriteBase, lineHeight, lineY, numLines;

    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;

    gfx_draw_string(dpi, (char *)"", TEXT_COLOUR_255, dpi->x, dpi->y);
    char *buffer = gCommonStringFormatBuffer;
    format_string(buffer, 256, STR_STRING, args);

    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;
    gfx_wrap_string(buffer, width, &numLines, &fontSpriteBase);
    lineHeight = font_get_line_height(fontSpriteBase);

    gCurrentFontFlags = 0;

    sint32 expectedY = y - (numLines * lineHeight);
    if (expectedY < 50) {
        return (numLines * lineHeight); // Skip drawing, return total height.
    }

    lineY = y;
    for (sint32 line = 0; line <= numLines; ++line) {
        gfx_draw_string(dpi, buffer, TEXT_COLOUR_254, x, lineY - (numLines * lineHeight));
        buffer = get_string_end(buffer) + 1;
        lineY += lineHeight;
    }
    return lineY - y;
}

// Wrap string without drawing, useful to get the height of a wrapped string.
// Almost the same as gfx_draw_string_left_wrapped
sint32 chat_string_wrapped_get_height(void *args, sint32 width)
{
    sint32 fontSpriteBase, lineHeight, lineY, numLines;

    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;

    char *buffer = gCommonStringFormatBuffer;
    format_string(buffer, 256, STR_STRING, args);

    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;
    gfx_wrap_string(buffer, width, &numLines, &fontSpriteBase);
    lineHeight = font_get_line_height(fontSpriteBase);

    gCurrentFontFlags = 0;

    lineY = 0;
    for (sint32 line = 0; line <= numLines; ++line) {
        buffer = get_string_end(buffer) + 1;
        lineY += lineHeight;
    }

    return lineY;
}

}
