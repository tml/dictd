/* dictdplugin_judy.c -- 
 * Created: Tue Aug  5 19:19:48 2003 by vle@gmx.net
 * Copyright 2003 Aleksey Cheusov <vle@gmx.net>
 * This program comes with ABSOLUTELY NO WARRANTY.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 1, or (at your option) any
 * later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * $Id: dictdplugin_judy.c,v 1.12 2003/08/11 17:06:27 cheusov Exp $
 * 
 */

#include "dictP.h"
#include "dictdplugin.h"
#include "data.h"
#include "str.h"
#include "include_regex.h"

#include <maa.h>
#include <Judy.h>

#if STRING_H
#include <string.h>
#endif

#include <stdio.h>
#include <errno.h>

#if HAVE_LIMITS_H
#include <limits.h>
#endif

#if WCTYPE_H
#include <ctype.h>
#endif

#if HAVE_WCTYPE_H
#include <wctype.h>
#endif

#define BUFSIZE 1024

#ifndef BOOL
#define BOOL char
#endif

/**********************************************************/
#define USE_INTERNAL_HEAP /* this may apeeds-up this plugin */

#ifdef USE_INTERNAL_HEAP

#include "heap.h"

#else

#define heap_create(heap, opts) (0);
#define heap_error(err_code) (NULL)
#define heap_destroy(heap) (0)
#define heap_alloc(heap, size) (xmalloc (size))
#define heap_strdup(heap, s) (xstrdup (s))
#define heap_free(heap, p) (p ? xfree (p), NULL : NULL)
#define heap_realloc(heap, p, size) (realloc (p, size))
#define heap_isempty(heap) (1)

#endif

typedef struct global_data_s {
   char m_err_msg  [BUFSIZE];

   void *m_heap;
   void *m_heap2;

//   char * m_res;
//   int m_res_size;

   int m_mres_count;
   const char ** m_mres;
   int *m_mres_sizes;

//   char *m_result_buf;

   int *m_offs_size_array;
//   int m_offs_size_array_size
   dictData *m_data;
//   int m_errno;

   int m_strat_exact;
   int m_strat_prefix;
   int m_strat_substring;
   int m_strat_suffix;
   int m_strat_re;
   int m_strat_regexp;
   int m_strat_soundex;
   int m_strat_lev;
   int m_strat_word;

   Pvoid_t m_judy_array;
   int m_max_hw_len;

   char m_conf_index_fn [NAME_MAX+1];
   char m_conf_data_fn  [NAME_MAX+1];
   BOOL m_conf_allchars;
   BOOL m_conf_utf8;
} global_data;

int dictdb_close (void *dict_data);
int dictdb_open (
   const dictPluginData *init_data,
   int init_data_size,
   int *version,
   void ** dict_data);
const char *dictdb_error (void *dict_data);
int dictdb_free (void * dict_data);
int dictdb_search (
   void *dict_data,
   const char *word, int word_size,
   int search_strategy,
   int *ret,
   const char **result_extra, int *result_extra_size,
   const char * const* *result,
   const int **result_sizes,
   int *results_count);

/**********************************************************/

static global_data * global_data_create (void)
{
   global_data *d = (global_data *) xmalloc (sizeof (*d));

   memset (d, 0, sizeof (*d));

   d -> m_strat_exact     = -1;
   d -> m_strat_prefix    = -1;
   d -> m_strat_substring = -1;
   d -> m_strat_suffix    = -1;
   d -> m_strat_re        = -1;
   d -> m_strat_regexp    = -1;
   d -> m_strat_soundex   = -1;
   d -> m_strat_lev       = -1;
   d -> m_strat_word      = -1;

   return d;
}

static void global_data_destroy (global_data *d)
{
   dictdb_free (d);

   if (d -> m_offs_size_array)
      xfree (d -> m_offs_size_array);

//   fprintf (stderr, "destroying...\n");
   JudySLFreeArray (&d -> m_judy_array, 0);
   heap_destroy (&d -> m_heap);
   heap_destroy (&d -> m_heap2);

   dict_data_close (d -> m_data);
   str_destroy ();

   if (d)
      xfree (d);
}

/********************************************************************/
static int const static_minus1_array [] = {
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static const int *alloc_minus1_array (int count)
{
   int *p;

   if (count <= sizeof (static_minus1_array) / sizeof (int)){
      return static_minus1_array;
   }else{
      p = xmalloc (count * sizeof (int));
      memset (p, -1, count * sizeof (int));
      return p;
   }
}

static void free_minus1_array (int *p)
{
   if (p != static_minus1_array && p){
      xfree (p);
   }
}

/********************************************************************/

static void set_strat (
   const dictPluginData_strategy * strat_data,
   global_data * dict_data)
{
   if (!strcmp (strat_data -> name, "exact")){
      dict_data -> m_strat_exact = strat_data -> number;
   }else if (!strcmp (strat_data -> name, "prefix")){
      dict_data -> m_strat_prefix = strat_data -> number;
   }else if (!strcmp (strat_data -> name, "suffix")){
      dict_data -> m_strat_suffix = strat_data -> number;
   }else if (!strcmp (strat_data -> name, "substring")){
      dict_data -> m_strat_substring = strat_data -> number;
   }else if (!strcmp (strat_data -> name, "re")){
      dict_data -> m_strat_re = strat_data -> number;
   }else if (!strcmp (strat_data -> name, "regexp")){
      dict_data -> m_strat_regexp = strat_data -> number;
   }else if (!strcmp (strat_data -> name, "soundex")){
      dict_data -> m_strat_soundex = strat_data -> number;
   }else if (!strcmp (strat_data -> name, "lev")){
      dict_data -> m_strat_lev = strat_data -> number;
   }else if (!strcmp (strat_data -> name, "word")){
      dict_data -> m_strat_word = strat_data -> number;
   }
}

static int process_line (char *s, void *data)
{
   char * value = NULL;
   global_data *dict_data = (global_data *) data;

//   fprintf (stderr, "line_to_be_processed='%s'\n", s);
//   fprintf (stderr, "err_msg='%s'\n", dict_data -> m_err_msg);

   value = strchr (s, '=');
   if (!value){
      snprintf (
	 dict_data -> m_err_msg,
	 BUFSIZE,
	 "invalid configure line: '%s'",
	 s);

//      fprintf (stderr, "value='%s'\n", value);
      return 1;
   }

//   fprintf (stderr, "line='%s'\n", (char *) s);

   *value++ = 0;

   if (!strcmp(s, "allchars")){
      if (strcmp (value, "0") && strcmp (value, "")){
	 dict_data -> m_conf_allchars = 1;
      }
   }else if (!strcmp(s, "utf8")){
      if (strcmp (value, "0") && strcmp (value, "")){
	 dict_data -> m_conf_utf8 = 1;
      }
   }else if (!strcmp(s, "index")){
      strlcpy (
	 dict_data -> m_conf_index_fn, value,
	 sizeof (dict_data -> m_conf_index_fn));
   }else if (!strcmp(s, "data")){
      strlcpy (
	 dict_data -> m_conf_data_fn, value,
	 sizeof (dict_data -> m_conf_data_fn));
   }

   return 0;
}

static void remove_spaces (char *s)
{
   char *p;

   for (p = s; *s; ){
      if (*s == '#'){
	 break;
      }

      if (*s != ' ')
	 *p++ = *s++;
      else
	 ++s;
   }

   *p = 0;
}

static void read_lines (
   char *buf, int len,
   void *data,
   int (*fun)(char *, void *))
{
   char *p     = NULL;
   int comment = 0;
   int i       = 0;
//   global_data *dict_data = (global_data *) data;

//   fprintf (stderr, "buffer='%s'\n", buf);

   for (i=0; i <= len; ++i){
//      fprintf (stderr, "curr_char='%c'\n", buf [i]);
      switch (buf [i]){
      case '\n':
      case '\0':
	 buf [i] = 0;
//	 fprintf (stderr, "curr_line='%s'\n", buf);

	 if (p){
	    remove_spaces (p);
	    if (*p){
	       if (fun (p, data))
		  return;
//
//	       if (dict_data -> m_err_msg [0]){
//		  fprintf (stderr, "WOW!!!\n");
//		  return;
//	       }
	    }
	 }

	 comment = 0;
	 p = NULL;
	 break;

      case '#':
	 comment = 1;
	 break;

      default:
	 if (!p && !isspace (buf [i]))
	    p = buf + i;
      }

      if (comment){
	 buf [i] = 0;
      }
   }
}

static void plugin_error (global_data *dict_data, const char *err_msg)
{
   strlcpy (dict_data -> m_err_msg, err_msg, BUFSIZE);
}

static BOOL split_index (
   global_data *dict_data,
   char *buf,
   unsigned long *def_offset,
   unsigned long *def_size)
{
   char *tab          = 0;
   char *def_offset_s = 0;
   char *def_size_s   = 0;

   tab = strchr (buf, '\t');
   if (!tab){
      plugin_error (dict_data, "corrupted index file");
      return 0;
   }

   *tab = 0;
   def_offset_s = tab + 1;

   tab = strchr (def_offset_s, '\t');
   if (!tab){
      plugin_error (dict_data, "corrupted index file");
      return 0;
   }
   *tab = 0;
   def_size_s = tab + 1;

//   fprintf (stderr, "offs=%s\n", def_offset_s);
//   fprintf (stderr, "size=%s\n", def_size_s);

   *def_offset = b64_decode (def_offset_s);
   *def_size   = b64_decode (def_size_s);

   return 1;
}

static void it_incr1 (
   global_data *dict_data,
   PPvoid_t value,
   const char* word,
   unsigned long offs,
   unsigned long size)
{
   ++ *(PWord_t) value;
}

/* iterate over entries WORD/VALUE in judy array JUDY */
#define JUDY_ITERATE(JUDY,VALUE,WORD)            \
   for (;                                        \
        VALUE;                                   \
        VALUE = JudySLNext (JUDY, WORD, 0))

#define JUDY_ITERATE_ALL(JUDY,VALUE,WORD)        \
   WORD [0] = 0;                                 \
   VALUE = JudySLFirst (JUDY, WORD, 0);          \
   JUDY_ITERATE(JUDY,VALUE,WORD)

/*
static void debug_print (global_data *dict_data)
{
   char word [BUFSIZE] = "";
   PPvoid_t value;

   assert (sizeof (word) > dict_data -> m_max_hw_len);

   JUDY_ITERATE_ALL (dict_data -> m_judy_array, value, word){
      fprintf (stderr, "%s --> %li\n", word, * (PWord_t) value);
   }
}
*/

//static TWord_t count2offs (global_data *dict_data);

static Word_t count2offs (global_data *dict_data)
{
   char word [BUFSIZE] = "";
   PPvoid_t value;
   Word_t sum = 0;
   Word_t val;

   assert (sizeof (word) > dict_data -> m_max_hw_len);
   JUDY_ITERATE_ALL (dict_data -> m_judy_array, value, word){
      val = (*(PWord_t) value);
      *(PWord_t) value = sum;
      sum += val;
   }

   return sum;
}

static void read_index_file (
   global_data *dict_data,
   void (*fun) (
      global_data *dict_data,
      PPvoid_t value,
      const char* word,
      unsigned long offs,
      unsigned long size))
{
   char buf [BUFSIZE];
   FILE *fd       = 0;
   PPvoid_t value = NULL;
   int word_count = 0;

   unsigned long def_offset;
   unsigned long def_size;

   int len;

   fd = fopen (dict_data -> m_conf_index_fn, "r");
   if (!fd){
      fprintf (stderr, "oops :((\n");
      plugin_error (dict_data, strerror(errno));
      return;
   }

   while (fgets (buf, BUFSIZE, fd) != (char *) NULL){
      if ('\n' == buf [strlen (buf) - 1])
	 buf [strlen (buf) - 1] = 0;

      if (!split_index (dict_data, buf, &def_offset, &def_size)){
	 fclose (fd);
	 return;
      }

      if (
	 tolower_alnumspace (
	    buf, buf,
	    dict_data -> m_conf_allchars, dict_data -> m_conf_utf8))
      {
	 plugin_error (dict_data, "tolower_alnumspace failed");
	 fclose (fd);
	 return;
      }

      len = strlen (buf);
      if (len > dict_data -> m_max_hw_len)
	 dict_data -> m_max_hw_len = len;

//      fprintf (stderr, "%s %li %li\n", buf, def_offset, def_size);
      value = JudySLIns (&dict_data -> m_judy_array, buf, 0);
      assert (value != (PPvoid_t) 0 && value != (PPvoid_t) -1);

      (*fun) (dict_data, value, buf, def_offset, def_size);
//      fprintf (stderr, "value=%li\n", *(PWord_t)value);
      ++word_count;
   }

   if (ferror (fd)){
      fclose (fd);
      plugin_error (dict_data, "reading error");
      return;
   }

//   fprintf (stderr, "%i\n", word_count);
   fclose (fd);
}

static void it_fill_array (
   global_data *dict_data,
   PPvoid_t value,
   const char* word,
   unsigned long offs,
   unsigned long size)
{
   Word_t val = * (PWord_t) value;

//   fprintf (
//      stderr,
//      "offs_zzz=%i\n",
//      dict_data -> m_offs_size_array [val + val + 0]);

   while (dict_data -> m_offs_size_array [val + val + 0] != -1){
      ++val;
   }

   dict_data -> m_offs_size_array [val + val + 0] = offs;
   dict_data -> m_offs_size_array [val + val + 1] = size;
}

static void init_index_file (global_data *dict_data)
{
   Word_t sum  = 0;
   size_t array_size = 0;

   char word [BUFSIZE]="";
   PPvoid_t value;
   Word_t val;

   dict_data -> m_judy_array = NULL;

   read_index_file (dict_data, it_incr1);
   if (dict_data -> m_err_msg [0])
      return;

//   debug_print (dict_data);

   sum = count2offs (dict_data);
   array_size = 2 * (sum/* + 1*/) * sizeof (int);

   assert (sizeof (word) > dict_data -> m_max_hw_len);

//   dict_data -> m_offs_size_array_size = sum;
   dict_data -> m_offs_size_array = xmalloc (array_size);
   memset (dict_data -> m_offs_size_array, -1, array_size);
//   dict_data -> m_offs_size_array [sum + sum];

   read_index_file (dict_data, it_fill_array);
   if (dict_data -> m_err_msg [0])
      return;

//   fputs ("ura1\n", stderr);
   JUDY_ITERATE_ALL (dict_data -> m_judy_array, value, word){
      val = *(PWord_t) value;
//      fprintf (stderr, "%s --> %li\n", word, val);
      *value = dict_data -> m_offs_size_array + val * 2;
//      fprintf (stderr, "%s --> %p\n", word, *value);
//      fprintf (stderr, "%s --> %li\n", word, *(PWord_t)value - (long)dict_data -> m_offs_size_array);
   }
//   return;

//   fputs ("ura2\n", stderr);
//   JUDY_ITERATE_ALL (dict_data -> m_judy_array, value, word){
//      fprintf (
//	 stderr,
//	 "%s --> %i %i\n",
//	 word,
//	 ((int *) *value) [0],
//	 ((int *) *value) [1]);
//   }
}

static void init_data_file (global_data *dict_data)
{
   assert (!dict_data -> m_data);

   dict_data -> m_data = dict_data_open (dict_data -> m_conf_data_fn, 0);
//   fprintf (stderr, "data=%p\n", dict_data -> m_data);
}

int dictdb_open (
   const dictPluginData *init_data,
   int init_data_size,
   int *version,
   void ** data)
{
   int i;
   int err;

   global_data *dict_data = global_data_create ();
//   fprintf (stderr, "err_msg_init='%s'\n", dict_data -> m_err_msg);

   err = heap_create (&dict_data -> m_heap, NULL);
   if (err){
      plugin_error (dict_data, heap_error (err));
      return 1;
   }

   err = heap_create (&dict_data -> m_heap2, NULL);
   if (err){
      plugin_error (dict_data, heap_error (err));
      return 1;
   }

   if (version)
      *version = 0;

   *data = (void *) dict_data;

//   int max_strat_num = -1;

   for (i=0; i < init_data_size; ++i){
      switch (init_data [i].id){
      case DICT_PLUGIN_INITDATA_STRATEGY:
	 set_strat (
	    (const dictPluginData_strategy * ) init_data [i].data,
	    dict_data);

	 break;

      case DICT_PLUGIN_INITDATA_DICT:
	 {
	    int len = init_data [i].size;
	    char *buf = NULL;

	    if (-1 == len)
	       len = strlen (init_data [i].data);

	    buf = xstrdup(init_data [i].data);

	    read_lines (buf, len, dict_data, process_line);
//	    fprintf (stderr, "err='%s'\n", dict_data -> m_err_msg);
	    if (dict_data -> m_err_msg [0]){
	       dictdb_free (dict_data);
	       return 1;
	    }

	    if (buf)
	       xfree (buf);

	    if (!dict_data -> m_conf_index_fn [0]){
	       plugin_error (dict_data, "missing 'index' option");
	       return 2;
	    }

	    if (!dict_data -> m_conf_data_fn [0]){
	       plugin_error (dict_data, "missing 'data' option");
	       return 2;
	    }
	 }
	 break;
      default:
	 break;
      }
   }

   init_index_file (dict_data);
   init_data_file  (dict_data);

   if (dict_data -> m_err_msg [0])
      return 1;

//   fprintf (stderr, "max_word_len = %i\n", dict_data -> m_max_hw_len);
   if (dict_data -> m_max_hw_len > BUFSIZE - 100){
      plugin_error (dict_data, "Index file contains too long word");
      return 1;
   }

//   debug_print (dict_data);

   return 0;
}

int dictdb_close (void *data)
{
   global_data_destroy (data);

   return 0;
}

const char *dictdb_error (void *dict_data)
{
   global_data *data = (global_data *)dict_data;

   if (data -> m_err_msg [0])
      return data -> m_err_msg;
//   else if (data -> m_errno)
//      return strerror (data -> m_errno);
   else
      return NULL;
}

int dictdb_free (void * data)
{
   int i;
   global_data *dict_data = (global_data *) data;

//   fprintf (stderr, "dictdb_free\n");

   if (dict_data){
      free_minus1_array (dict_data -> m_mres_sizes);
      dict_data -> m_mres_sizes = NULL;

      for (i = 0; i < dict_data -> m_mres_count; ++i){
	 heap_free (dict_data -> m_heap, (void *) dict_data -> m_mres [i]);
      }
      dict_data -> m_mres_count = 0;

      heap_free (dict_data -> m_heap2, dict_data -> m_mres);
      dict_data -> m_mres = NULL;

      dict_data -> m_err_msg [0] = 0;

//      xfree (dict_data -> m_data);
//      dict_data -> m_data = NULL;
   }

   return 0;
}

/* set dict_data -> m_mres_count  and dict_data -> m_mres */
static int match_exact (
   global_data *dict_data,
   const char *word)
{
   int const * const *result_curr;

   if (!word [0])
      return 0;

   result_curr = (int const *const *) JudySLGet (
      dict_data -> m_judy_array, word, 0);

   if (!result_curr){
//	 fprintf (stderr, "EHHHHH!!!!\n");
      return 0;
   }

   dict_data -> m_mres = (const char **)
      heap_alloc (dict_data -> m_heap2, sizeof (dict_data -> m_mres [0]));

   dict_data -> m_mres [0] =
      heap_strdup (dict_data -> m_heap, word);

   dict_data -> m_mres_count = 1;

   return 0;
}

static int match_substring (
   global_data *dict_data,
   const char *word)
{
   PPvoid_t value;

   char curr_word [BUFSIZE];

   curr_word [0] = 0;

   JUDY_ITERATE_ALL (dict_data -> m_judy_array, value, curr_word){
      if (!strstr (curr_word, word))
	 continue;

      ++dict_data -> m_mres_count;

      dict_data -> m_mres = (const char **)
	 heap_realloc (
	    dict_data -> m_heap2,
	    dict_data -> m_mres,
	    dict_data -> m_mres_count * sizeof (dict_data -> m_mres [0]));

      dict_data -> m_mres [dict_data -> m_mres_count - 1] =
	 heap_strdup (dict_data -> m_heap, curr_word);
   }

   return 0;
}

static int match_prefix (
   global_data *dict_data,
   const char *word)
{
   PPvoid_t value;
   PPvoid_t value_last;

   char curr_word [BUFSIZE];
   size_t len = strlen (word);
   int cmp_res;

   strlcpy (curr_word, word, sizeof (curr_word));

   if (len){
      /* trick:
	 here we construct a word that is "largest" one
	 with the prefix 'word' (function argument)
      */
      curr_word [len + 0] = UCHAR_MAX;
      curr_word [len + 1] = UCHAR_MAX;
      curr_word [len + 2] = UCHAR_MAX;
      curr_word [len + 3] = UCHAR_MAX;
      curr_word [len + 4] = UCHAR_MAX;
      curr_word [len + 5] = UCHAR_MAX;
      curr_word [len + 6] = UCHAR_MAX;
      curr_word [len + 7] = 0;

      value_last = JudySLPrev (dict_data -> m_judy_array, curr_word, 0);
//      fprintf (stderr, "last=%s %p\n", curr_word, value_last);
      curr_word [len + 0] = 0;
   }else{
      value_last = NULL;
   }

   strlcpy (curr_word, word, sizeof (curr_word));
   value = JudySLGet (dict_data -> m_judy_array, curr_word, 0);
   if (!value)
      value = JudySLNext (dict_data -> m_judy_array, curr_word, 0);

//   fprintf (stderr, "first=%s %p\n", curr_word, value);

   for (
      ;
      value;
      value = JudySLNext (dict_data -> m_judy_array, curr_word, 0))
   {
      if (value == value_last){
	 cmp_res = strncmp (word, curr_word, len);

	 if (cmp_res){
//	    fprintf (stderr, "%s != %s\n", word, curr_word);
	    break;
	 }
      }

      ++dict_data -> m_mres_count;

      dict_data -> m_mres = (const char **)
	 heap_realloc (
	    dict_data -> m_heap2,
	    dict_data -> m_mres,
	    dict_data -> m_mres_count * sizeof (dict_data -> m_mres [0]));

      dict_data -> m_mres [dict_data -> m_mres_count - 1] =
	 heap_strdup (dict_data -> m_heap, curr_word);

      if (value == value_last){
	 break;
      }
   }

   return 0;
}

static int match_suffix (
   global_data *dict_data,
   const char *word)
{
   PPvoid_t value;

   char curr_word [BUFSIZE];

   size_t len = strlen (word);
   size_t curr_len;

   curr_word [0] = 0;

   JUDY_ITERATE_ALL (dict_data -> m_judy_array, value, curr_word){
      curr_len = strlen (curr_word);
      if (curr_len < len)
	 continue;

      if (strncmp (word, curr_word + curr_len - len, len))
	 continue;

      ++dict_data -> m_mres_count;

      dict_data -> m_mres = (const char **)
	 heap_realloc (
	    dict_data -> m_heap2,
	    dict_data -> m_mres,
	    dict_data -> m_mres_count * sizeof (dict_data -> m_mres [0]));

      dict_data -> m_mres [dict_data -> m_mres_count - 1] =
	 heap_strdup (dict_data -> m_heap, curr_word);
   }

   return 0;
}

static int match_soundex (
   global_data *dict_data,
   const char *word)
{
   PPvoid_t value;

   char curr_word [BUFSIZE];

   char soundex  [5];
   char soundex2 [5];

   curr_word [0] = 0;

   txt_soundex2 (word, soundex);

   JUDY_ITERATE_ALL (dict_data -> m_judy_array, value, curr_word){
      txt_soundex2 (curr_word, soundex2);

      if (strcmp (soundex, soundex2))
	 continue;

      ++dict_data -> m_mres_count;

      dict_data -> m_mres = (const char **)
	 heap_realloc (
	    dict_data -> m_heap2,
	    dict_data -> m_mres,
	    dict_data -> m_mres_count * sizeof (dict_data -> m_mres [0]));

      dict_data -> m_mres [dict_data -> m_mres_count - 1] =
	 heap_strdup (dict_data -> m_heap, curr_word);
   }

   return 0;
}

#define CHECK \
   if (buf [0]){                                              \
      value = JudySLGet (dict_data -> m_judy_array, buf, 0);  \
      if (value && strcmp (prev_buf, buf)){                   \
         strlcpy (prev_buf, buf, BUFSIZE);                    \
                                                              \
         ++dict_data -> m_mres_count;                         \
                                                              \
         dict_data -> m_mres = (const char **)                \
            heap_realloc (                                    \
               dict_data -> m_heap2,                          \
               dict_data -> m_mres,                           \
               dict_data -> m_mres_count                      \
                  * sizeof (dict_data -> m_mres [0]));        \
         dict_data -> m_mres [dict_data -> m_mres_count - 1] =\
            heap_strdup (dict_data -> m_heap, buf);           \
      }                                                       \
   }

static int match_lev (
   global_data *dict_data,
   const char *word)
{
   size_t len;
   char buf [BUFSIZE];
   int i, j, k;
   char *p;
   char tmp;
   PPvoid_t value;
   char prev_buf [BUFSIZE] = "";

   static char const c [] = "qwertyuiopasdfghjklzxcvbnm0123456789"; /* fix this*/
   static int const charcount = sizeof (c) - 1;

//   strlcpy (curr_word, word, sizeof (curr_word));

   len = strlen (word);
   if (len >= BUFSIZE)
      len = BUFSIZE - 10;

                                /* Transpositions */
   strlcpy( buf, word, sizeof (buf) );
   CHECK; /* checking word inself */
   for (i = 1; i < len; i++) {
      tmp = buf [i-1];
      buf [i-1] = buf [i];
      buf [i] = tmp;

      CHECK;

      tmp = buf [i-1];
      buf [i-1] = buf [i];
      buf [i] = tmp;
   }

				/* Deletions */
   for (i = 0; i < len; i++) {
      p = buf;
      for (j = 0; j < len; j++)
	 if (i != j)
	    *p++ = word [j];

      *p = '\0';
      CHECK;
   }

				/* Insertions */
   for (i = 0; i < len; i++) {
      for (k = 0; k < charcount; k++) {
	 p = buf;
         for (j = 0; j < len; j++) {
            *p++ = word [j];
            if (i == j)
	       *p++ = c [k];
         }
         *p = '\0';
	 CHECK;
      }
   }

                                /* Insertions at the beginning */
   strlcpy (buf + 1, word, BUFSIZE - 1);
   for (k = 0; k < charcount; k++) {
      buf [0] = c [k];
      CHECK;
   }

                                  /* Substitutions */
   strlcpy (buf, word, BUFSIZE);
   for (i = 0; i < len; i++) {
      for (j = 0; j < charcount; j++) {
	 if (buf [i] != c [j]){
	    tmp = buf [i];
	    buf [i] = c [j];

	    CHECK;

	    buf [i] = tmp;
	 }
      }
   }

   return 0;
}

static int match_regexp (
   global_data *dict_data,
   const char *word,

   int regex_flags)
{
   PPvoid_t value;
   regex_t  re;
   int      err;

   char curr_word [BUFSIZE];

   if ((err = regcomp (&re, word, REG_ICASE | REG_NOSUB | regex_flags))) {
      regerror (err, &re, dict_data -> m_err_msg, sizeof (dict_data -> m_err_msg));
      regfree (&re);
      return 0;
   }

   curr_word [0] = 0;

   JUDY_ITERATE_ALL (dict_data -> m_judy_array, value, curr_word){
      if (regexec(&re, curr_word, 0, NULL, 0))
	 continue;

      ++dict_data -> m_mres_count;

      dict_data -> m_mres = (const char **)
	 heap_realloc (
	    dict_data -> m_heap2,
	    dict_data -> m_mres,
	    dict_data -> m_mres_count * sizeof (dict_data -> m_mres [0]));

      dict_data -> m_mres [dict_data -> m_mres_count - 1] =
	 heap_strdup (dict_data -> m_heap, curr_word);
   }

   regfree (&re);

   return 0;
}

static int match_word (
   global_data *dict_data,
   const char *word)
{
   static char const prefix_re [] = "(^|[[:space:][:punct:]])"; /* fix this */
   static char const suffix_re [] = "($|[[:space:][:punct:]])";

   char buf [BUFSIZE];
   char *p;

   if (sizeof (prefix_re) + sizeof (suffix_re) + 3 * strlen (word) > BUFSIZE){
      /* too long word */
      return 0;
   }

   strcpy (buf, prefix_re);

   p = buf + strlen (buf);
   for (; *word; ++word){
      switch (*word){
      case '?':
      case '(':
      case ')':
      case '|':
      case '[':
      case ']':
      case '+':
      case '*':
      case '.':
      case '^':
      case '$':
	 *p++ = '[';
	 *p++ = *word;
	 *p++ = ']';
	 break;
      case '\\':
	 *p++ = '\\';
	 *p++ = '\\';
	 break;
      default:
	 *p++ = *word;
      }
   }

   *p++ = 0;

   strcat (buf, suffix_re);
//   fprintf (stderr, "re=%s\n", buf);

   return match_regexp (
      dict_data, buf, REG_EXTENDED);
}

int dictdb_search (
   void *data,
   const char *word, int word_size,
   int search_strategy,
   int *ret,
   const char **result_extra, int *result_extra_size,
   const char * const* *result,
   const int **result_sizes,
   int *results_count)
{
//   char *word_copy = NULL;
   int match_search_type;
//   int const * const *result_next;
   char word_copy2 [BUFSIZE];
//   int cnt = 0;
//   int i   = 0;
   int exit_code = 0;

   global_data *dict_data = (global_data *) data;

//   dictdb_free (data);

   if (result_extra)
      *result_extra      = NULL;
   if (result_extra_size)
      *result_extra_size = 0;
   if (result_sizes)
      *result_sizes     = NULL;

   *ret = DICT_PLUGIN_RESULT_NOTFOUND;

   if (-1 == word_size){
      word_size = strlen (word);
   }

   match_search_type = search_strategy & DICT_MATCH_MASK;
   search_strategy &= ~DICT_MATCH_MASK;

   assert (!dict_data -> m_mres);
   assert (!dict_data -> m_mres_sizes);
   assert (!dict_data -> m_mres_count);
   assert (heap_isempty (dict_data -> m_heap));

   strlcpy (word_copy2, word, sizeof (word_copy2));

   if (
      search_strategy != dict_data -> m_strat_regexp &&
      search_strategy != dict_data -> m_strat_re)
   {
      if (
	 tolower_alnumspace (
	    word_copy2, word_copy2,
	    dict_data -> m_conf_allchars, dict_data -> m_conf_utf8))
      {
	 plugin_error (dict_data, "tolower_alnumspace failed");
	 return 1;
      }

      if (word_size > dict_data -> m_max_hw_len){
//      fprintf (stderr, "This word is too long\n");
	 return 0;
      }

//      fprintf (stderr, "lowe-cased: %s\n", word_copy2);
   }

   if (match_search_type){
      /* MATCH command */

      dict_data -> m_mres_count = 0;

      if (search_strategy == dict_data -> m_strat_exact){
	 exit_code = match_exact (
	    dict_data, word_copy2);
      }else if (search_strategy == dict_data -> m_strat_word){
	 exit_code = match_word (
	    dict_data, word_copy2);
      }else if (search_strategy == dict_data -> m_strat_substring){
	 exit_code = match_substring (
	    dict_data, word_copy2);
      }else if (search_strategy == dict_data -> m_strat_prefix){
	 exit_code = match_prefix (
	    dict_data, word_copy2);
      }else if (search_strategy == dict_data -> m_strat_suffix){
	 exit_code = match_suffix (
	    dict_data, word_copy2);
      }else if (search_strategy == dict_data -> m_strat_re){
	 exit_code = match_regexp (
	    dict_data, word_copy2, REG_EXTENDED);
      }else if (search_strategy == dict_data -> m_strat_regexp){
	 exit_code = match_regexp (
	    dict_data, word_copy2, 0);
      }else if (search_strategy == dict_data -> m_strat_soundex){
	 exit_code = match_soundex (
	    dict_data, word_copy2);
      }else if (search_strategy == dict_data -> m_strat_lev){
	 exit_code = match_lev (
	    dict_data, word_copy2);
      }

      if (exit_code)
	 return exit_code;

      if (!dict_data -> m_mres_count)
	 return 0;

      dict_data -> m_mres_sizes =
	 (int *) alloc_minus1_array (dict_data -> m_mres_count);

      *result        = dict_data -> m_mres;
      *result_sizes  = dict_data -> m_mres_sizes;
      *results_count = dict_data -> m_mres_count;

      *ret = DICT_PLUGIN_RESULT_FOUND;

      return 0;
   }else{
      /* DEFINE command */

      int const * const * offs_size;
      int const * const * offs_size_next;
      int cnt;
      int i;

      if (!word_copy2 [0])
	 return 0;

      offs_size = (int const *const *) JudySLGet (
	 dict_data -> m_judy_array, word_copy2, 0);

      if (!offs_size)
	 return 0;

      offs_size_next = (const int *const *) JudySLNext (
	 dict_data -> m_judy_array, word_copy2, 0);

      if (offs_size_next){
	 cnt = (const int *) *offs_size_next - (const int *) *offs_size;
	 cnt /= 2;
      }else{
	 cnt = 1;/* fix this */;
      }

      dict_data -> m_mres =
	 (const char **) heap_alloc (
	    dict_data -> m_heap2,
	    cnt * sizeof (dict_data -> m_mres [0]));
      dict_data -> m_mres_sizes = (int *) alloc_minus1_array (cnt);

      dict_data -> m_mres_count = cnt;

      for (i = 0; i < cnt; ++i){
	 dict_data -> m_mres [i] =
	    dict_data_read_ (
	       dict_data -> m_data,
	       (*offs_size) [0], (*offs_size) [1],
	       NULL, NULL);
      }

      *result        = dict_data -> m_mres;
      *result_sizes  = dict_data -> m_mres_sizes;
      *results_count = cnt;
      *ret           = DICT_PLUGIN_RESULT_FOUND;

      return 0;
   }
}