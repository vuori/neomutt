/**
 * @file
 * Parse ANSI Sequences
 *
 * @authors
 * Copyright (C) 2023 Richard Russon <rich@flatcap.org>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @page color_parse_ansi Parse ANSI Sequences
 *
 * Parse ANSI Sequences
 */

#include "config.h"
#include <stddef.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include "mutt/lib.h"
#include "gui/lib.h"
#include "parse_ansi.h"
#include "ansi.h"
#include "color.h"

/**
 * ansi_is_end_char - Is this the end of a sequence?
 * @param c Character to test
 * @retval true Is it a valid end char
 */
static inline bool ansi_is_end_char(char c)
{
  return ((c == 'm') || (c == ';'));
}

/**
 * ansi_skip_sequence - Skip an unrecognised sequence
 * @param str String to examine
 * @retval num Number of characters to skip over
 */
int ansi_skip_sequence(const char *str)
{
  if (!str || (str[0] == '\0'))
    return 0;

  int count = 1;
  while ((str[0] != '\0') && !ansi_is_end_char(str[0]))
  {
    str++;
    count++;
  }

  return count;
}

/**
 * ansi_color_seq_length - Is this an ANSI escape sequence?
 * @param str String to test
 * @retval  0 No, not an ANSI sequence
 * @retval >0 Length of the ANSI sequence
 *
 * Match ANSI escape sequences of type 'm', e.g.
 * - `<esc>[1;32m`
 */
int ansi_color_seq_length(const char *str)
{
  if (!str || !*str)
    return 0;

  if ((str[0] != '\033') || (str[1] != '[')) // Escape
    return 0;

  int i = 2;
  while ((str[i] != '\0') && (isdigit(str[i]) || (str[i] == ';')))
  {
    i++;
  }

  if (str[i] == 'm')
    return i + 1;

  return 0;
}

/**
 * ansi_color_parse_single - Parse a single ANSI escape sequence
 * @param buf     String to parse
 * @param ansi    AnsiColor for the result
 * @param dry_run Don't parse anything, just skip over
 * @retval num Length of the escape sequence
 *
 * Parse an ANSI escape sequence into @a ansi.
 * Calling this function repeatedly, will accumulate sequences in @a ansi.
 */
int ansi_color_parse_single(const char *buf, struct AnsiColor *ansi, bool dry_run)
{
  int seq_len = ansi_color_seq_length(buf);
  if (seq_len == 0)
    return 0;

  if (dry_run || !ansi)
    return seq_len;

  int pos = 2; // Skip '<esc>['

  while (pos < seq_len)
  {
    if ((buf[pos] == '0') && isdigit(buf[pos + 1]))
    {
      pos++; // Skip the leading zero
    }
    else if ((buf[pos] == '0') && ansi_is_end_char(buf[pos + 1]))
    {
      ansi->fg = COLOR_DEFAULT;
      ansi->bg = COLOR_DEFAULT;
      ansi->attrs = 0;
      ansi->attr_color = NULL;
      pos += 2;
    }
    else if ((buf[pos] == '1') && ansi_is_end_char(buf[pos + 1]))
    {
      ansi->attrs |= A_BOLD;
      pos += 2;
    }
    else if ((buf[pos] == '3') && ansi_is_end_char(buf[pos + 1]))
    {
      ansi->attrs |= A_ITALIC;
      pos += 2;
    }
    else if (buf[pos] == '3')
    {
      // 30-37 basic foreground
      if ((buf[pos + 1] >= '0') && (buf[pos + 1] < '8') && ansi_is_end_char(buf[pos + 2]))
      {
        ansi->fg = buf[pos + 1] - '0';
        pos += 3;
      }
      else if (buf[pos + 1] == '8')
      {
        if (mutt_str_startswith(buf + pos, "38;5;") && isdigit(buf[pos + 5]))
        {
          // 38;5;n palette foreground
          char *end = NULL;
          int value = strtoul(buf + pos + 5, &end, 10);
          if ((value >= 0) && (value < 256) && end && ansi_is_end_char(end[0]))
          {
            ansi->fg = value;
            pos += end - &buf[pos];
          }
          else
          {
            pos += ansi_skip_sequence(buf + pos);
          }
        }
        else if (mutt_str_startswith(buf + pos, "38;2;") && isdigit(buf[pos + 5]))
        {
          // 38;2;R;G;B true colour foreground
          pos += ansi_skip_sequence(buf + pos + 5);
          pos += ansi_skip_sequence(buf + pos);
          pos += ansi_skip_sequence(buf + pos);
        }
        else
        {
          return pos;
        }
      }
      else if ((buf[pos + 1] == '9') && ansi_is_end_char(buf[pos + 2]))
      {
        // default fg
        ansi->fg = COLOR_DEFAULT;
        pos += 2;
      }
      else
      {
        pos += ansi_skip_sequence(buf + pos);
      }
    }
    else if ((buf[pos] == '4') && ansi_is_end_char(buf[pos + 1]))
    {
      ansi->attrs |= A_UNDERLINE;
      pos += 2;
    }
    else if (buf[pos] == '4')
    {
      // 40-47 basic background
      if ((buf[pos + 1] >= '0') && (buf[pos + 1] < '8'))
      {
        ansi->bg = buf[pos + 1] - '0';
        pos += 3;
      }
      else if (buf[pos + 1] == '8')
      {
        if (mutt_str_startswith(buf + pos, "48;5;") && isdigit(buf[pos + 5]))
        {
          // 48;5;n palette background
          char *end = NULL;
          int value = strtoul(buf + pos + 5, &end, 10);
          if ((value >= 0) && (value < 256) && end && ansi_is_end_char(end[0]))
          {
            ansi->bg = value;
            pos += end - &buf[pos];
          }
          else
          {
            pos += ansi_skip_sequence(buf + pos);
          }
        }
        else if (mutt_str_startswith(buf + pos, "48;2;") && isdigit(buf[pos + 5]))
        {
          // 48;2;R;G;B true colour background
          pos += ansi_skip_sequence(buf + pos + 5);
          pos += ansi_skip_sequence(buf + pos);
          pos += ansi_skip_sequence(buf + pos);
        }
        else
        {
          pos += ansi_skip_sequence(buf + pos);
        }
      }
      else if ((buf[pos + 1] == '9') && ansi_is_end_char(buf[pos + 2]))
      {
        // default background
        ansi->bg = COLOR_DEFAULT;
        pos += 2;
      }
    }
    else if ((buf[pos] == '5') && ansi_is_end_char(buf[pos + 1]))
    {
      ansi->attrs |= A_BLINK;
      pos += 2;
    }
    else if ((buf[pos] == '7') && ansi_is_end_char(buf[pos + 1]))
    {
      ansi->attrs |= A_REVERSE;
      pos += 2;
    }
    else
    {
      while ((pos < seq_len) && (buf[pos] != ';'))
        pos++;
    }
  }

  return pos;
}
