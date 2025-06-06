/* tilde.c -- Tilde expansion code (~/foo := $HOME/foo). */

/* Copyright (C) 1988-2020 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library (Readline), a library
   for reading lines of text with interactive input and history editing.

   Readline is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Readline is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Readline.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "sys.h"

#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

#if defined(HAVE_UNISTD_H)
#ifdef _MINIX
#include <sys/types.h>
#endif
#include <unistd.h>
#endif

#if defined(HAVE_STRING_H)
#include <string.h>
#else /* !HAVE_STRING_H */
#include <strings.h>
#endif /* !HAVE_STRING_H */

#if defined(HAVE_STDLIB_H)
#include <stdlib.h>
#else
#include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <sys/types.h>
#if defined(HAVE_PWD_H)
#include <pwd.h>
#endif

#ifdef _WIN32
#include <userenv.h>
#endif

#include "tilde.h"

#if defined(TEST) || defined(STATIC_MALLOC)
static void *xmalloc(), *xrealloc();
#else
// #  include "xmalloc.h"
#endif /* TEST || STATIC_MALLOC */

#if !defined(HAVE_GETPW_DECLS)
#if defined(HAVE_GETPWUID)
extern struct passwd *getpwuid(uid_t);
#endif
#if defined(HAVE_GETPWNAM)
extern struct passwd *getpwnam(const char *);
#endif
#endif /* !HAVE_GETPW_DECLS */

#if !defined(savestring)
#define savestring(x) strcpy((char *) xmalloc(1 + strlen(x)), (x))
#endif /* !savestring */

#if !defined(NULL)
#if defined(__STDC__)
#define NULL ((void *) 0)
#else
#define NULL 0x0
#endif /* !__STDC__ */
#endif /* !NULL */

/* If being compiled as part of bash, these will be satisfied from
   variables.o.  If being compiled as part of readline, they will
   be satisfied from shell.o. */
extern char *sh_get_home_dir(void);
extern char *sh_get_env_value(const char *);

/* The default value of tilde_additional_prefixes.  This is set to
   whitespace preceding a tilde so that simple programs which do not
   perform any word separation get desired behaviour. */
static const char *default_prefixes[] = {" ~", "\t~", (const char *) NULL};

/* The default value of tilde_additional_suffixes.  This is set to
   whitespace or newline so that simple programs which do not
   perform any word separation get desired behaviour. */
static const char *default_suffixes[] = {" ", "\n", (const char *) NULL};

/* If non-null, this contains the address of a function that the application
   wants called before trying the standard tilde expansions.  The function
   is called with the text sans tilde, and returns a malloc()'ed string
   which is the expansion, or a NULL pointer if the expansion fails. */
tilde_hook_func_t *tilde_expansion_preexpansion_hook = (tilde_hook_func_t *) NULL;

/* If non-null, this contains the address of a function to call if the
   standard meaning for expanding a tilde fails.  The function is called
   with the text (sans tilde, as in "foo"), and returns a malloc()'ed string
   which is the expansion, or a NULL pointer if there is no expansion. */
tilde_hook_func_t *tilde_expansion_failure_hook = (tilde_hook_func_t *) NULL;

/* When non-null, this is a NULL terminated array of strings which
   are duplicates for a tilde prefix.  Bash uses this to expand
   `=~' and `:~'. */
char **tilde_additional_prefixes = (char **) default_prefixes;

/* When non-null, this is a NULL terminated array of strings which match
   the end of a username, instead of just "/".  Bash sets this to
   `:' and `=~'. */
char **tilde_additional_suffixes = (char **) default_suffixes;

static int tilde_find_prefix(const char *, int *);
static int tilde_find_suffix(const char *);
static char *isolate_tilde_prefix(const char *, int *);
static char *glue_prefix_and_suffix(char *, const char *, int);

/* Find the start of a tilde expansion in STRING, and return the index of
   the tilde which starts the expansion.  Place the length of the text
   which identified this tilde starter in LEN, excluding the tilde itself. */
static int tilde_find_prefix(const char *string, int *len)
{
  register int i, j, string_len;
  register char **prefixes;

  prefixes = tilde_additional_prefixes;

  string_len = strlen(string);
  *len = 0;

  if (*string == '\0' || *string == '~')
    return (0);

  if (prefixes)
  {
    for (i = 0; i < string_len; i++)
    {
      for (j = 0; prefixes[j]; j++)
      {
        if (strncmp(string + i, prefixes[j], strlen(prefixes[j])) == 0)
        {
          *len = strlen(prefixes[j]) - 1;
          return (i + *len);
        }
      }
    }
  }
  return (string_len);
}

/* Find the end of a tilde expansion in STRING, and return the index of
   the character which ends the tilde definition.  */
static int tilde_find_suffix(const char *string)
{
  register int i, j, string_len;
  register char **suffixes;

  suffixes = tilde_additional_suffixes;
  string_len = strlen(string);

  for (i = 0; i < string_len; i++)
  {
    if (IS_DIRSEP(string[i]))
      break;

    for (j = 0; suffixes && suffixes[j]; j++)
    {
      if (strncmp(string + i, suffixes[j], strlen(suffixes[j])) == 0)
        return (i);
    }
  }
  return (i);
}

/* Return a new string which is the result of tilde expanding STRING. */
char *tilde_expand(const char *string)
{
  char *result;
  int result_size, result_index;

  result_index = result_size = 0;
  if (result = strchr(string, '~'))
    result = (char *) xmalloc(result_size = (strlen(string) + 16));
  else
    result = (char *) xmalloc(result_size = (strlen(string) + 1));

  /* Scan through STRING expanding tildes as we come to them. */
  while (1)
  {
    register int start, end;
    char *tilde_word, *expansion;
    int len;

    /* Make START point to the tilde which starts the expansion. */
    start = tilde_find_prefix(string, &len);

    /* Copy the skipped text into the result. */
    if ((result_index + start + 1) > result_size)
      result = (char *) xrealloc(result, 1 + (result_size += (start + 20)));

    strncpy(result + result_index, string, start);
    result_index += start;

    /* Advance STRING to the starting tilde. */
    string += start;

    /* Make END be the index of one after the last character of the
       username. */
    end = tilde_find_suffix(string);

    /* If both START and END are zero, we are all done. */
    if (!start && !end)
      break;

    /* Expand the entire tilde word, and copy it into RESULT. */
    tilde_word = (char *) xmalloc(1 + end);
    strncpy(tilde_word, string, end);
    tilde_word[end] = '\0';
    string += end;

    expansion = tilde_expand_word(tilde_word);

    if (expansion == 0)
      expansion = tilde_word;
    else
      free(tilde_word);

    len = strlen(expansion);
#if defined(__CYGWIN__) || defined(_WIN32)
    /* Fix for Cygwin to prevent ~user/xxx from expanding to //xxx when
       $HOME for `user' is /.  On cygwin, // denotes a network drive. */
    if (len > 1 || !IS_DIRSEP(*expansion) || !IS_DIRSEP(*string))
#endif
    {
      if ((result_index + len + 1) > result_size)
        result = (char *) xrealloc(result, 1 + (result_size += (len + 20)));

      strcpy(result + result_index, expansion);
      result_index += len;
    }
    free(expansion);
  }

  result[result_index] = '\0';

  return (result);
}

/* Take FNAME and return the tilde prefix we want expanded.  If LENP is
   non-null, the index of the end of the prefix into FNAME is returned in
   the location it points to. */
static char *isolate_tilde_prefix(const char *fname, int *lenp)
{
  char *ret;
  int i;

  ret = (char *) xmalloc(strlen(fname));
  for (i = 1; fname[i] && !IS_DIRSEP(fname[i]); i++)
    ret[i - 1] = fname[i];
  ret[i - 1] = '\0';
  if (lenp)
    *lenp = i;
  return ret;
}

#if 0
/* Public function to scan a string (FNAME) beginning with a tilde and find
   the portion of the string that should be passed to the tilde expansion
   function.  Right now, it just calls tilde_find_suffix and allocates new
   memory, but it can be expanded to do different things later. */
char *
tilde_find_word (const char *fname, int flags, int *lenp)
{
  int x;
  char *r;

  x = tilde_find_suffix (fname);
  if (x == 0)
    {
      r = savestring (fname);
      if (lenp)
	*lenp = 0;
    }
  else
    {
      r = (char *)xmalloc (1 + x);
      strncpy (r, fname, x);
      r[x] = '\0';
      if (lenp)
	*lenp = x;
    }

  return r;
}
#endif

/* Return a string that is PREFIX concatenated with SUFFIX starting at
   SUFFIND. */
static char *glue_prefix_and_suffix(char *prefix, const char *suffix, int suffind)
{
  char *ret;
  int plen, slen;

  plen = (prefix && *prefix) ? strlen(prefix) : 0;
  slen = strlen(suffix + suffind);
  ret = (char *) xmalloc(plen + slen + 1);
  if (plen)
    strcpy(ret, prefix);
  strcpy(ret + plen, suffix + suffind);
  return ret;
}

/* Do the work of tilde expansion on FILENAME.  FILENAME starts with a
   tilde.  If there is no expansion, call tilde_expansion_failure_hook.
   This always returns a newly-allocated string, never static storage. */
char *tilde_expand_word(const char *filename)
{
  char *dirname, *expansion, *username;
  int user_len;
  struct passwd *user_entry;

  if (filename == 0)
    return ((char *) NULL);

  if (*filename != '~')
    return (savestring(filename));

  /* A leading `~/' or a bare `~' is *always* translated to the value of
     $HOME or the home directory of the current user, regardless of any
     preexpansion hook. */
  if (filename[1] == '\0' || IS_DIRSEP(filename[1]))
  {
    /* Prefix $HOME to the rest of the string. */
    expansion = sh_get_env_value("HOME"); // not normally set in windows, but set by cygin/msys
#if defined(_WIN32)
    if (expansion == 0)
      expansion = sh_get_env_value("USERPROFILE"); // what windows sets for the users homedir
#endif

    /* If there is no HOME variable, look up the directory in
       the password database. */
    if (expansion == 0)
      expansion = sh_get_home_dir();

    return (glue_prefix_and_suffix(expansion, filename, 1));
  }

  username = isolate_tilde_prefix(filename, &user_len);

  if (tilde_expansion_preexpansion_hook)
  {
    expansion = (*tilde_expansion_preexpansion_hook)(username);
    if (expansion)
    {
      dirname = glue_prefix_and_suffix(expansion, filename, user_len);
      free(username);
      free(expansion);
      return (dirname);
    }
  }

  /* No preexpansion hook, or the preexpansion hook failed.  Look in the
     password database. */
  dirname = (char *) NULL;
#if defined(HAVE_GETPWNAM)
  user_entry = getpwnam(username);
#else
  user_entry = 0;
#endif
  if (user_entry == 0)
  {
    /* If the calling program has a special syntax for expanding tildes,
       and we couldn't find a standard expansion, then let them try. */
    if (tilde_expansion_failure_hook)
    {
      expansion = (*tilde_expansion_failure_hook)(username);
      if (expansion)
      {
        dirname = glue_prefix_and_suffix(expansion, filename, user_len);
        free(expansion);
      }
    }
#ifdef _WIN32
    if (dirname == 0)
    {
      char profile_dir[MAX_PATH];
      DWORD size = MAX_PATH;
      if (GetProfilesDirectoryA(profile_dir, &size))
      {
        if ((strlen(profile_dir) + strlen(username)) < (MAX_PATH - 1))
        {
          profile_dir[strlen(profile_dir) + 1] = 0;
          profile_dir[strlen(profile_dir)] = DIR_SEPARATOR;

          strcat(profile_dir, username);
          dirname = savestring(profile_dir);
        }
      }
    }
#endif // _WIN32

    /* If we don't have a failure hook, or if the failure hook did not
       expand the tilde, return a copy of what we were passed. */
    if (dirname == 0)
      dirname = savestring(filename);
  }
#if defined(HAVE_GETPWENT)
  else
    dirname = glue_prefix_and_suffix(user_entry->pw_dir, filename, user_len);
#endif

  free(username);
#if defined(HAVE_GETPWENT)
  endpwent();
#endif
  return (dirname);
}

#if defined(TEST)
#undef NULL
#include <stdio.h>

main(int argc, char **argv)
{
  char *result, line[512];
  int done = 0;

  while (!done)
  {
    printf("~expand: ");
    fflush(stdout);

    if (!gets(line))
      strcpy(line, "done");

    if ((strcmp(line, "done") == 0) || (strcmp(line, "quit") == 0) || (strcmp(line, "exit") == 0))
    {
      done = 1;
      break;
    }

    result = tilde_expand(line);
    printf("  --> %s\n", result);
    free(result);
  }
  exit(0);
}

static void memory_error_and_abort(void);

static void *xmalloc(size_t bytes)
{
  void *temp = (char *) malloc(bytes);

  if (!temp)
    memory_error_and_abort();
  return (temp);
}

static void *xrealloc(void *pointer, int bytes)
{
  void *temp;

  if (!pointer)
    temp = malloc(bytes);
  else
    temp = realloc(pointer, bytes);

  if (!temp)
    memory_error_and_abort();

  return (temp);
}

static void memory_error_and_abort(void)
{
  fprintf(stderr, "readline: out of virtual memory\n");
  abort();
}

/*
 * Local variables:
 * compile-command: "gcc -g -DTEST -o tilde tilde.c"
 * end:
 */
#endif /* TEST */
