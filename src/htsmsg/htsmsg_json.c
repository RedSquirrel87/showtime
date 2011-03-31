/*
 *  Functions converting HTSMSGs to/from JSON
 *  Copyright (C) 2008 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "htsmsg_json.h"
#include "htsbuf.h"
#include "misc/string.h"
#include "misc/json.h"


/*
 * Note to future:
 * If your about to add support for numbers with decimal point,
 * remember to always serialize them with '.' as decimal point character
 * no matter what current locale says. This is according to the JSON spec.
 */
static void
htsmsg_json_write(htsmsg_t *msg, htsbuf_queue_t *hq, int isarray,
		  int indent, int pretty)
{
  htsmsg_field_t *f;
  char buf[30];
  static const char *indentor = "\n\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

  htsbuf_append(hq, isarray ? "[" : "{", 1);

  TAILQ_FOREACH(f, &msg->hm_fields, hmf_link) {

    if(pretty) 
      htsbuf_append(hq, indentor, indent < 16 ? indent : 16);

    if(!isarray) {
      htsbuf_append_and_escape_jsonstr(hq, f->hmf_name ?: "noname");
      htsbuf_append(hq, ": ", 2);
    }

    switch(f->hmf_type) {
    case HMF_MAP:
      htsmsg_json_write(&f->hmf_msg, hq, 0, indent + 1, pretty);
      break;

    case HMF_LIST:
      htsmsg_json_write(&f->hmf_msg, hq, 1, indent + 1, pretty);
      break;

    case HMF_STR:
      htsbuf_append_and_escape_jsonstr(hq, f->hmf_str);
      break;

    case HMF_BIN:
      htsbuf_append_and_escape_jsonstr(hq, "binary");
      break;

    case HMF_S64:
      snprintf(buf, sizeof(buf), "%" PRId64, f->hmf_s64);
      htsbuf_append(hq, buf, strlen(buf));
      break;

    default:
      abort();
    }

    if(TAILQ_NEXT(f, hmf_link))
      htsbuf_append(hq, ",", 1);
  }
  
  if(pretty) 
    htsbuf_append(hq, indentor, indent-1 < 16 ? indent-1 : 16);
  htsbuf_append(hq, isarray ? "]" : "}", 1);
}

/**
 *
 */
int
htsmsg_json_serialize(htsmsg_t *msg, htsbuf_queue_t *hq, int pretty)
{
  htsmsg_json_write(msg, hq, msg->hm_islist, 2, pretty);
  if(pretty) 
    htsbuf_append(hq, "\n", 1);
  return 0;
}


/**
 *
 */

static void *
create_map(void *opaque)
{
  return htsmsg_create_map();
}

static void *
create_list(void *opaque)
{
  return htsmsg_create_list();
}

static void
destroy_obj(void *opaque, void *obj)
{
  return htsmsg_destroy(obj);
}

static void
add_obj(void *opaque, void *parent, const char *name, void *child)
{
  htsmsg_add_msg(parent, name, child);
}

static void 
add_string(void *opaque, void *parent, const char *name,  char *str)
{
  htsmsg_add_str(parent, name, str);
  free(str);
}

static void 
add_long(void *opaque, void *parent, const char *name, long v)
{
  htsmsg_add_s64(parent, name, v);
}

static void 
add_double(void *opaque, void *parent, const char *name, double v)
{
  htsmsg_add_s64(parent, name, v);
}

static void 
add_bool(void *opaque, void *parent, const char *name, int v)
{
  htsmsg_add_u32(parent, name, v);
}

static void 
add_null(void *opaque, void *parent, const char *name)
{
}

/**
 *
 */
static const json_deserializer_t json_to_htsmsg = {
  .jd_create_map      = create_map,
  .jd_create_list     = create_list,
  .jd_destroy_obj     = destroy_obj,
  .jd_add_obj         = add_obj,
  .jd_add_string      = add_string,
  .jd_add_long        = add_long,
  .jd_add_double      = add_double,
  .jd_add_bool        = add_bool,
  .jd_add_null        = add_null,
};


/**
 *
 */
htsmsg_t *
htsmsg_json_deserialize(const char *src)
{
  return json_deserialize(src, &json_to_htsmsg, NULL);
}
