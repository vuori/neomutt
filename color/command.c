/**
 * @file
 * Parse colour commands
 *
 * @authors
 * Copyright (C) 2021 Richard Russon <rich@flatcap.org>
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
 * @page color_command Parse colour commands
 *
 * Parse NeoMutt 'color', 'uncolor', 'mono' and 'unmono' commands.
 */

#include "config.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "mutt/lib.h"
#include "core/lib.h"
#include "gui/lib.h"
#include "mutt.h"
#include "parse/lib.h"
#include "color.h"
#include "command2.h"
#include "curses2.h"
#include "debug.h"
#include "globals.h"
#include "notify2.h"
#include "parse_color.h"
#include "quoted.h"
#include "regex4.h"
#include "simple2.h"

/**
 * ColorFields - Mapping of colour names to their IDs
 */
const struct Mapping ColorFields[] = {
  // clang-format off
  { "attachment",        MT_COLOR_ATTACHMENT },
  { "attach_headers",    MT_COLOR_ATTACH_HEADERS },
  { "body",              MT_COLOR_BODY },
  { "bold",              MT_COLOR_BOLD },
  { "error",             MT_COLOR_ERROR },
  { "hdrdefault",        MT_COLOR_HDRDEFAULT },
  { "header",            MT_COLOR_HEADER },
  { "index",             MT_COLOR_INDEX },
  { "index_author",      MT_COLOR_INDEX_AUTHOR },
  { "index_collapsed",   MT_COLOR_INDEX_COLLAPSED },
  { "index_date",        MT_COLOR_INDEX_DATE },
  { "index_flags",       MT_COLOR_INDEX_FLAGS },
  { "index_label",       MT_COLOR_INDEX_LABEL },
  { "index_number",      MT_COLOR_INDEX_NUMBER },
  { "index_size",        MT_COLOR_INDEX_SIZE },
  { "index_subject",     MT_COLOR_INDEX_SUBJECT },
  { "index_tag",         MT_COLOR_INDEX_TAG },
  { "index_tags",        MT_COLOR_INDEX_TAGS },
  { "indicator",         MT_COLOR_INDICATOR },
  { "italic",            MT_COLOR_ITALIC },
  { "markers",           MT_COLOR_MARKERS },
  { "message",           MT_COLOR_MESSAGE },
  { "normal",            MT_COLOR_NORMAL },
  { "options",           MT_COLOR_OPTIONS },
  { "progress",          MT_COLOR_PROGRESS },
  { "prompt",            MT_COLOR_PROMPT },
  { "quoted",            MT_COLOR_QUOTED },
  { "search",            MT_COLOR_SEARCH },
#ifdef USE_SIDEBAR
  { "sidebar_background", MT_COLOR_SIDEBAR_BACKGROUND },
  { "sidebar_divider",   MT_COLOR_SIDEBAR_DIVIDER },
  { "sidebar_flagged",   MT_COLOR_SIDEBAR_FLAGGED },
  { "sidebar_highlight", MT_COLOR_SIDEBAR_HIGHLIGHT },
  { "sidebar_indicator", MT_COLOR_SIDEBAR_INDICATOR },
  { "sidebar_new",       MT_COLOR_SIDEBAR_NEW },
  { "sidebar_ordinary",  MT_COLOR_SIDEBAR_ORDINARY },
  { "sidebar_spool_file", MT_COLOR_SIDEBAR_SPOOLFILE },
  { "sidebar_spoolfile", MT_COLOR_SIDEBAR_SPOOLFILE }, // This will be deprecated
  { "sidebar_unread",    MT_COLOR_SIDEBAR_UNREAD },
#endif
  { "signature",         MT_COLOR_SIGNATURE },
  { "status",            MT_COLOR_STATUS },
  { "stripe_even",       MT_COLOR_STRIPE_EVEN},
  { "stripe_odd",        MT_COLOR_STRIPE_ODD},
  { "tilde",             MT_COLOR_TILDE },
  { "tree",              MT_COLOR_TREE },
  { "underline",         MT_COLOR_UNDERLINE },
  { "warning",           MT_COLOR_WARNING },
  { NULL, 0 },
  // clang-format on
};

/**
 * ComposeColorFields - Mapping of compose colour names to their IDs
 */
const struct Mapping ComposeColorFields[] = {
  // clang-format off
  { "header",            MT_COLOR_COMPOSE_HEADER },
  { "security_encrypt",  MT_COLOR_COMPOSE_SECURITY_ENCRYPT },
  { "security_sign",     MT_COLOR_COMPOSE_SECURITY_SIGN },
  { "security_both",     MT_COLOR_COMPOSE_SECURITY_BOTH },
  { "security_none",     MT_COLOR_COMPOSE_SECURITY_NONE },
  { NULL, 0 }
  // clang-format on
};

/**
 * get_colorid_name - Get the name of a color id
 * @param cid Colour, e.g. #MT_COLOR_HEADER
 * @param buf Buffer for result
 */
void get_colorid_name(unsigned int cid, struct Buffer *buf)
{
  const char *name = NULL;

  if ((cid >= MT_COLOR_COMPOSE_HEADER) && (cid <= MT_COLOR_COMPOSE_SECURITY_SIGN))
  {
    name = mutt_map_get_name(cid, ComposeColorFields);
    if (name)
    {
      buf_printf(buf, "compose %s", name);
      return;
    }
  }

  name = mutt_map_get_name(cid, ColorFields);
  if (name)
    buf_addstr(buf, name);
  else
    buf_printf(buf, "UNKNOWN %d", cid);
}

/**
 * parse_object - Identify a colour object
 * @param[in]  buf   Temporary Buffer space
 * @param[in]  s     Buffer containing string to be parsed
 * @param[out] cid   Object type, e.g. #MT_COLOR_TILDE
 * @param[out] ql    Quote level, if type #MT_COLOR_QUOTED
 * @param[out] err   Buffer for error messages
 * @retval #CommandResult Result e.g. #MUTT_CMD_SUCCESS
 *
 * Identify a colour object, e.g. "quoted", "compose header"
 */
static enum CommandResult parse_object(struct Buffer *buf, struct Buffer *s,
                                       enum ColorId *cid, int *ql, struct Buffer *err)
{
  int rc;

  if (mutt_str_startswith(buf->data, "quoted") != 0)
  {
    int val = 0;
    if (buf->data[6] != '\0')
    {
      if (!mutt_str_atoi_full(buf->data + 6, &val) || (val > COLOR_QUOTES_MAX))
      {
        buf_printf(err, _("%s: no such object"), buf->data);
        return MUTT_CMD_WARNING;
      }
    }

    *ql = val;
    *cid = MT_COLOR_QUOTED;
    return MUTT_CMD_SUCCESS;
  }

  if (mutt_istr_equal(buf->data, "compose"))
  {
    if (!MoreArgs(s))
    {
      buf_printf(err, _("%s: too few arguments"), "color");
      return MUTT_CMD_WARNING;
    }

    parse_extract_token(buf, s, TOKEN_NO_FLAGS);

    rc = mutt_map_get_value(buf->data, ComposeColorFields);
    if (rc == -1)
    {
      buf_printf(err, _("%s: no such object"), buf->data);
      return MUTT_CMD_WARNING;
    }

    *cid = rc;
    return MUTT_CMD_SUCCESS;
  }

  rc = mutt_map_get_value(buf->data, ColorFields);
  if (rc == -1)
  {
    buf_printf(err, _("%s: no such object"), buf->data);
    return MUTT_CMD_WARNING;
  }
  else
  {
    color_debug(LL_DEBUG5, "object: %s\n", mutt_map_get_name(rc, ColorFields));
  }

  *cid = rc;
  return MUTT_CMD_SUCCESS;
}

/**
 * parse_uncolor - Parse an 'uncolor' command
 * @param buf     Temporary Buffer space
 * @param s       Buffer containing string to be parsed
 * @param err     Buffer for error messages
 * @param uncolor If true, 'uncolor', else 'unmono'
 * @retval #CommandResult Result e.g. #MUTT_CMD_SUCCESS
 *
 * Usage:
 * * uncolor index pattern [pattern...]
 * * unmono  index pattern [pattern...]
 */
static enum CommandResult parse_uncolor(struct Buffer *buf, struct Buffer *s,
                                        struct Buffer *err, bool uncolor)
{
  parse_extract_token(buf, s, TOKEN_NO_FLAGS);

  if (mutt_str_equal(buf->data, "*"))
  {
    colors_cleanup();
    return MUTT_CMD_SUCCESS;
  }

  unsigned int cid = MT_COLOR_NONE;
  int ql = 0;
  color_debug(LL_DEBUG5, "uncolor: %s\n", buf_string(buf));
  enum CommandResult rc = parse_object(buf, s, &cid, &ql, err);
  if (rc != MUTT_CMD_SUCCESS)
    return rc;

  if (cid == -1)
  {
    buf_printf(err, _("%s: no such object"), buf->data);
    return MUTT_CMD_ERROR;
  }

  if (cid == MT_COLOR_QUOTED)
  {
    color_debug(LL_DEBUG5, "quoted\n");
    return quoted_colors_parse_uncolor(cid, ql, err);
  }

  if ((cid == MT_COLOR_STATUS) && !MoreArgs(s))
  {
    color_debug(LL_DEBUG5, "simple\n");
    simple_color_reset(cid); // default colour for the status bar
    return MUTT_CMD_SUCCESS;
  }

  if (!mutt_color_has_pattern(cid))
  {
    color_debug(LL_DEBUG5, "simple\n");
    simple_color_reset(cid);
    return MUTT_CMD_SUCCESS;
  }

  if (OptNoCurses)
  {
    do
    {
      color_debug(LL_DEBUG5, "do nothing\n");
      /* just eat the command, but don't do anything real about it */
      parse_extract_token(buf, s, TOKEN_NO_FLAGS);
    } while (MoreArgs(s));

    return MUTT_CMD_SUCCESS;
  }

  bool changes = false;
  if (!MoreArgs(s))
  {
    if (regex_colors_parse_uncolor(cid, NULL, uncolor))
      return MUTT_CMD_SUCCESS;
    else
      return MUTT_CMD_ERROR;
  }

  do
  {
    parse_extract_token(buf, s, TOKEN_NO_FLAGS);
    if (mutt_str_equal("*", buf->data))
    {
      if (regex_colors_parse_uncolor(cid, NULL, uncolor))
        return MUTT_CMD_SUCCESS;
      else
        return MUTT_CMD_ERROR;
    }

    changes |= regex_colors_parse_uncolor(cid, buf->data, uncolor);

  } while (MoreArgs(s));

  if (changes)
    regex_colors_dump_all();

  return MUTT_CMD_SUCCESS;
}

/**
 * parse_color - Parse a 'color' command
 * @param buf      Temporary Buffer space
 * @param s        Buffer containing string to be parsed
 * @param err      Buffer for error messages
 * @param callback Function to handle command - Implements ::parser_callback_t
 * @param dry_run  If true, test the command, but don't apply it
 * @param color    If true "color", else "mono"
 * @retval #CommandResult Result e.g. #MUTT_CMD_SUCCESS
 *
 * Usage:
 * * color OBJECT [ ATTRS ] FG BG [ REGEX ]
 * * mono  OBJECT   ATTRS         [ REGEX ]
 */
static enum CommandResult parse_color(struct Buffer *buf, struct Buffer *s,
                                      struct Buffer *err, parser_callback_t callback,
                                      bool dry_run, bool color)
{
  int attrs = 0, q_level = 0;
  color_t fg = 0, bg = 0;
  unsigned int match = 0;
  enum ColorId cid = MT_COLOR_NONE;
  enum CommandResult rc;

  if (!MoreArgs(s))
  {
    if (StartupComplete)
    {
      color_dump();
      return MUTT_CMD_SUCCESS;
    }

    buf_printf(err, _("%s: too few arguments"), "color");
    return MUTT_CMD_WARNING;
  }

  parse_extract_token(buf, s, TOKEN_NO_FLAGS);
  color_debug(LL_DEBUG5, "color: %s\n", buf_string(buf));

  rc = parse_object(buf, s, &cid, &q_level, err);
  if (rc != MUTT_CMD_SUCCESS)
    return rc;

  rc = callback(buf, s, &fg, &bg, &attrs, err);
  if (rc != MUTT_CMD_SUCCESS)
    return rc;

  /* extract a regular expression if needed */

  if (mutt_color_has_pattern(cid) && (cid != MT_COLOR_STATUS))
  {
    color_debug(LL_DEBUG5, "regex needed\n");
    if (MoreArgs(s))
    {
      parse_extract_token(buf, s, TOKEN_NO_FLAGS);
    }
    else
    {
      buf_strcpy(buf, ".*");
    }
  }

  if (MoreArgs(s) && (cid != MT_COLOR_STATUS))
  {
    buf_printf(err, _("%s: too many arguments"), color ? "color" : "mono");
    return MUTT_CMD_WARNING;
  }

  if (dry_run)
  {
    color_debug(LL_DEBUG5, "dry_run bailout\n");
    *s->dptr = '\0'; /* fake that we're done parsing */
    return MUTT_CMD_SUCCESS;
  }

  /* The case of the tree object is special, because a non-default fg color of
   * the tree element may be combined dynamically with the default bg color of
   * an index line, not necessarily defined in a rc file.  */
  if (!OptNoCurses &&
      ((fg == COLOR_DEFAULT) || (bg == COLOR_DEFAULT) || (cid == MT_COLOR_TREE)) &&
      (use_default_colors() != OK))
  {
    buf_strcpy(err, _("default colors not supported"));
    return MUTT_CMD_ERROR;
  }

  if (regex_colors_parse_color_list(cid, buf->data, fg, bg, attrs, &rc, err))
  {
    color_debug(LL_DEBUG5, "regex_colors_parse_color_list done\n");
    return rc;
    // do nothing
  }
  else if (quoted_colors_parse_color(cid, fg, bg, attrs, q_level, &rc, err))
  {
    color_debug(LL_DEBUG5, "quoted_colors_parse_color done\n");
    return rc;
    // do nothing
  }
  else if ((cid == MT_COLOR_STATUS) && MoreArgs(s))
  {
    color_debug(LL_DEBUG5, "status\n");
    /* 'color status fg bg' can have up to 2 arguments:
     * 0 arguments: sets the default status color (handled below by else part)
     * 1 argument : colorize pattern on match
     * 2 arguments: colorize nth submatch of pattern */
    parse_extract_token(buf, s, TOKEN_NO_FLAGS);

    if (MoreArgs(s))
    {
      struct Buffer tmp = buf_make(0);
      parse_extract_token(&tmp, s, TOKEN_NO_FLAGS);
      if (!mutt_str_atoui_full(tmp.data, &match))
      {
        buf_printf(err, _("%s: invalid number: %s"), color ? "color" : "mono", tmp.data);
        buf_dealloc(&tmp);
        return MUTT_CMD_WARNING;
      }
      buf_dealloc(&tmp);
    }

    if (MoreArgs(s))
    {
      buf_printf(err, _("%s: too many arguments"), color ? "color" : "mono");
      return MUTT_CMD_WARNING;
    }

    rc = regex_colors_parse_status_list(cid, buf->data, fg, bg, attrs, match, err);
    return rc;
  }
  else // Remaining simple colours
  {
    color_debug(LL_DEBUG5, "simple\n");
    if (simple_color_set(cid, fg, bg, attrs))
      rc = MUTT_CMD_SUCCESS;
    else
      rc = MUTT_CMD_ERROR;
  }

  if (rc == MUTT_CMD_SUCCESS)
  {
    get_colorid_name(cid, buf);
    color_debug(LL_DEBUG5, "NT_COLOR_SET: %s\n", buf->data);
    struct EventColor ev_c = { cid, NULL };
    notify_send(ColorsNotify, NT_COLOR, NT_COLOR_SET, &ev_c);
  }

  return rc;
}

/**
 * mutt_parse_uncolor - Parse the 'uncolor' command - Implements Command::parse() - @ingroup command_parse
 */
enum CommandResult mutt_parse_uncolor(struct Buffer *buf, struct Buffer *s,
                                      intptr_t data, struct Buffer *err)
{
  if (OptNoCurses)
  {
    *s->dptr = '\0'; /* fake that we're done parsing */
    return MUTT_CMD_SUCCESS;
  }
  color_debug(LL_DEBUG5, "parse: %s\n", buf_string(buf));
  enum CommandResult rc = parse_uncolor(buf, s, err, true);
  curses_colors_dump();
  return rc;
}

/**
 * mutt_parse_unmono - Parse the 'unmono' command - Implements Command::parse() - @ingroup command_parse
 */
enum CommandResult mutt_parse_unmono(struct Buffer *buf, struct Buffer *s,
                                     intptr_t data, struct Buffer *err)
{
  *s->dptr = '\0'; /* fake that we're done parsing */
  return MUTT_CMD_SUCCESS;
}

/**
 * mutt_parse_color - Parse the 'color' command - Implements Command::parse() - @ingroup command_parse
 */
enum CommandResult mutt_parse_color(struct Buffer *buf, struct Buffer *s,
                                    intptr_t data, struct Buffer *err)
{
  bool dry_run = OptNoCurses;

  color_debug(LL_DEBUG5, "parse: %s\n", buf_string(buf));
  enum CommandResult rc = parse_color(buf, s, err, parse_color_pair, dry_run, true);
  curses_colors_dump();
  return rc;
}

/**
 * mutt_parse_mono - Parse the 'mono' command - Implements Command::parse() - @ingroup command_parse
 */
enum CommandResult mutt_parse_mono(struct Buffer *buf, struct Buffer *s,
                                   intptr_t data, struct Buffer *err)
{
  return parse_color(buf, s, err, parse_attr_spec, false, false);
}
